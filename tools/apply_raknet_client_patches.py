#!/usr/bin/env python3
"""
Applies the FakeBots *client mode* patches to a pristine copy of the
openmultiplayer/RakNet fork (commit recorded in UPSTREAM_COMMIT.txt).

The patched result is what lives in third_party/raknet/. This script exists so
the (small) change set is reviewable and reproducible:

    python3 tools/apply_raknet_client_patches.py <pristine_raknet_dir> <output_dir>

Every replacement is asserted to match exactly once, so a changed upstream
fails loudly instead of silently producing a broken network layer.
"""
import re
import sys
import shutil
from pathlib import Path


def sub_once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"patch '{what}': expected exactly 1 match, found {n}")
    return text.replace(old, new)


def patch_socketlayer(t):
    # --- receive: server -> client datagrams are plain RakNet --------------
    start = t.index("\t\tif (len > 10 && data[0] == 'S' && data[1] == 'A' && data[2] == 'M' && data[3] == 'P')")
    end_marker = "\t\treturn 1;\n\t}\n\telse\n\t{\n\t\t*errorCode = 0;"
    end = t.index(end_marker, start)
    new_recv = (
        "\t\t// FakeBots (client mode): datagrams sent by the server are plain RakNet - only\n"
        "\t\t// client -> server traffic is obfuscated - so there is nothing to decode and the\n"
        "\t\t// query ('SAMP') handler does not apply.\n"
        "\t\tunsigned short portnum = ntohs( sa.sin_port );\n"
        "\t\tProcessNetworkPacket(sa.sin_addr.s_addr, portnum, data, len, rakPeer);\n"
    )
    t = t[:start] + new_recv + t[end:]

    # --- send: obfuscate every outgoing datagram ---------------------------
    start = t.index("\tdo\n\t{\n\t\t// TODO - use WSASendTo which is faster.")
    end = t.index("\twhile ( len == 0 );", start) + len("\twhile ( len == 0 );")
    new_send = (
        "\t// FakeBots (client mode): every datagram that leaves a client is obfuscated for the\n"
        "\t// destination (server) port exactly like a real SA-MP client does. The scratch\n"
        "\t// buffer is on the stack: many bot network threads call SendTo() concurrently.\n"
        "\tuint8_t scratch[ MAXIMUM_MTU_SIZE + 16 ];\n"
        "\tconst int outLen = SAMPRakNet::EncryptClientDatagram( ( const uint8_t* ) data, length, port, scratch, ( int ) sizeof( scratch ) );\n"
        "\tif ( outLen < 0 )\n"
        "\t\treturn 1;\n"
        "\n"
        "\tdo\n"
        "\t{\n"
        "\t\tlen = sendto( s, ( char* ) scratch, outLen, 0, ( const sockaddr* ) & sa, sizeof( struct sockaddr_in ) );\n"
        "\t}\n"
        "\twhile ( len == 0 );"
    )
    t = t[:start] + new_send + t[end:]
    return t


def patch_socketlayer_reuse(t):
    # Linux lets several UDP sockets that all set SO_REUSEADDR bind the very same
    # (ephemeral) port.  With hundreds of bots two of them sooner or later got the same
    # local port - the server then sees ONE peer (ip:port) instead of two, answers the
    # second handshake with ID_CONNECTION_ATTEMPT_FAILED and the datagrams of both bots
    # get mixed.  A client has no reason to share its port.
    t = sub_once(
        t,
        "\tif (setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, ( char * ) & sock_opt, sizeof ( sock_opt ) ) == -1)\n",
        "\t// FakeBots (client mode): never share a local port between two bots\n"
        "\tint noReuse = 0;\n"
        "\tif (setsockopt( listenSocket, SOL_SOCKET, SO_REUSEADDR, ( char * ) & noReuse, sizeof ( noReuse ) ) == -1)\n",
        "no SO_REUSEADDR",
    )
    return t


def patch_socketlayer_h(t):
    # Upstream deletes the process-wide SocketLayer in ~RakPeer(). With one RakPeer per bot
    # that deleted the layer (and on Windows ran WSACleanup) under the feet of every other bot,
    # and the lazy `new` was racy between bot threads.
    t = sub_once(
        t,
        "\t\tstatic inline SocketLayer* Instance()\n"
        "\t\t{\n"
        "\t\t\tif (_instance == nullptr)\n"
        "\t\t\t\t_instance = new SocketLayer();\n"
        "\t\t\treturn _instance;\n"
        "\t\t}\n"
        "\t\tstatic inline void Destroy()\n"
        "\t\t{\n"
        "\t\t\tif (_instance != nullptr)\n"
        "\t\t\t{\n"
        "\t\t\t\tdelete _instance;\n"
        "\t\t\t\t_instance = nullptr;\n"
        "\t\t\t}\n"
        "\t\t}\n",
        "\t\t// FakeBots (client mode): one layer shared by every bot of the process. It is created\n"
        "\t\t// thread-safely on first use and lives until the plugin is unloaded.\n"
        "\t\tstatic inline SocketLayer* Instance()\n"
        "\t\t{\n"
        "\t\t\tstatic SocketLayer instance;\n"
        "\t\t\treturn &instance;\n"
        "\t\t}\n"
        "\t\t// A destroyed RakPeer must not tear the shared layer down for the other bots.\n"
        "\t\tstatic inline void Destroy() {}\n",
        "SocketLayer singleton",
    )
    return t


def patch_singleproducerconsumer_h(t):
    # The list nodes are allocated with `new DataPlusPtr` but released through a `char*`
    # (undefined behaviour; AddressSanitizer reports 298 new-delete-type-mismatch errors
    # per few bots).  Delete them as what they are.
    n = t.count("delete (char*) readPointer;") + t.count("delete (char*) writePointer;")
    if n != 3:
        raise SystemExit(f"patch 'SingleProducerConsumer delete': expected 3 matches, found {n}")
    t = t.replace("delete (char*) readPointer;", "delete (DataPlusPtr*) readPointer;")
    t = t.replace("delete (char*) writePointer;", "delete (DataPlusPtr*) writePointer;")
    return t


def patch_rakpeer_h(t):
    t = sub_once(
        t,
        "\t\t\tunsigned char requestsMade;\n",
        "\t\t\tunsigned char requestsMade;\n"
        "\t\t\tunsigned short cookie; // FakeBots: connection cookie handed out by the server\n",
        "RequestedConnectionStruct.cookie",
    )
    t = sub_once(
        t,
        "\t\tbool ParseConnectionAuthPacket(RakPeer::RemoteSystemStruct* remoteSystem, PlayerID playerId, unsigned char* data, int byteSize);\n",
        "\t\tbool ParseConnectionAuthPacket(RakPeer::RemoteSystemStruct* remoteSystem, PlayerID playerId, unsigned char* data, int byteSize);\n"
        "\t\t// FakeBots (client mode): answers the server's ID_AUTH_KEY challenge.\n"
        "\t\tvoid HandleAuthKeyChallenge(RakPeer::RemoteSystemStruct* remoteSystem, PlayerID playerId, const unsigned char* data, int byteSize);\n",
        "HandleAuthKeyChallenge decl",
    )
    t = t.replace("(e.g., NPCs)", "(e.g., internal use)")
    return t


def patch_rakpeer_cpp(t):
    # cookie starts at 0 for every new connection attempt (2 places)
    old = "\trcs->requestsMade=0;\n"
    if t.count(old) != 2:
        raise SystemExit("patch 'cookie init': expected 2 matches")
    t = t.replace(old, "\trcs->requestsMade=0;\n\trcs->cookie=0;\n")

    # number of open-connection attempts is configurable (was a hard-coded 6)
    t = sub_once(
        t,
        "condition1=rcs->requestsMade==6;",
        "condition1=rcs->requestsMade>=SAMPRakNet::GetMaxConnectionAttempts();",
        "max connection attempts",
    )

    # open-connection request carries (cookie ^ 0x6969) and no uninitialised byte
    t = sub_once(
        t,
        "\t\t\t\tc[0] = ID_OPEN_CONNECTION_REQUEST;\n\t\t\t\tc[1] = 0; // Pad - apparently some routers block 1 byte packets\n",
        "\t\t\t\tc[0] = ID_OPEN_CONNECTION_REQUEST;\n"
        "\t\t\t\t{\n"
        "\t\t\t\t\t// FakeBots: first request goes out with cookie 0, the server answers with its\n"
        "\t\t\t\t\t// cookie (ID_OPEN_CONNECTION_COOKIE) and the repeated request carries it XOR 0x6969.\n"
        "\t\t\t\t\tconst unsigned short xordCookie = (unsigned short)(rcs->cookie ^ SAMPRakNet::COOKIE_XOR);\n"
        "\t\t\t\t\tmemcpy(&c[1], &xordCookie, sizeof(xordCookie));\n"
        "\t\t\t\t}\n",
        "open connection request payload",
    )

    # drop the server-side "NPC" auth branch completely
    t = sub_once(
        t,
        "\t\tif (authStr == StringView(\"NPC\", 4)) {\n"
        "\t\t\tremoteSystem->sampData.authType = SAMPRakNet::AuthType_NPC;\n"
        "\t\t\tAcceptConnectionRequest(remoteSystem);\n"
        "\t\t\treturn true;\n"
        "\t\t}\n"
        "\t\telse if (SAMPRakNet::CheckAuth(remoteSystem->sampData.authIndex, authStr)) {",
        "\t\tif (SAMPRakNet::CheckAuth(remoteSystem->sampData.authIndex, authStr)) {",
        "remove NPC auth branch",
    )

    # debug traces of the handshake (printed only when FAKEBOTS_DEBUG=1)
    t = sub_once(
        t,
        "\t\t\t\trcs->requestsMade++;\n\t\t\t\trcs->nextRequestTime=timeMS+1000;\n",
        "\t\t\t\trcs->requestsMade++;\n\t\t\t\trcs->nextRequestTime=timeMS+1000;\n"
        "\t\t\t\tSAMPRakNet::GetCore()->printLn(\"peer %p: open-connection request #%u cookie=0x%04x\", (void*)this, (unsigned)rcs->requestsMade, (unsigned)rcs->cookie);\n",
        "trace request",
    )
    t = sub_once(
        t,
        "\n\t\t\t\t\t\tpacket->data[ 0 ] = ID_CONNECTION_ATTEMPT_FAILED; // Attempted a connection and couldn't\n",
        "\n\t\t\t\t\t\tpacket->data[ 0 ] = ID_CONNECTION_ATTEMPT_FAILED; // Attempted a connection and couldn't\n"
        "\t\t\t\t\t\tSAMPRakNet::GetCore()->printLn(\"peer %p: connection attempt failed after %u requests (cookie=0x%04x)\", (void*)this, (unsigned)rcs->requestsMade, (unsigned)rcs->cookie);\n",
        "trace failure",
    )

    # new method: answer the ID_AUTH_KEY challenge
    t = sub_once(
        t,
        "bool RakPeer::ParseConnectionAuthPacket(",
        "// FakeBots (client mode)\n"
        "// The server answers our ID_CONNECTION_REQUEST with [ID_AUTH_KEY][u8 len][challenge].\n"
        "// A genuine client replies with the matching 40 character response from the SA-MP\n"
        "// table: [ID_AUTH_KEY][u8 len][response]. Handled here (network thread) so the join\n"
        "// does not depend on how busy the server's main thread is.\n"
        "void RakPeer::HandleAuthKeyChallenge(RakPeer::RemoteSystemStruct* remoteSystem, PlayerID playerId, const unsigned char* data, int byteSize)\n"
        "{\n"
        "\t(void)remoteSystem;\n"
        "\tif (byteSize < 2)\n"
        "\t\treturn;\n"
        "\tconst unsigned int challengeLen = data[1];\n"
        "\tif ((int)(2 + challengeLen) > byteSize)\n"
        "\t\treturn;\n"
        "\n"
        "\tconst char* response = 0;\n"
        "\tsize_t responseLen = 0;\n"
        "\tif (!SAMPRakNet::FindAuthResponse((const char*)data + 2, challengeLen, &response, &responseLen))\n"
        "\t{\n"
        "\t\tchar hex[160] = {0};\n"
        "\t\tfor (unsigned i = 0; i < challengeLen && i < 48; ++i) sprintf(hex + i * 3, \"%02X \", (unsigned)data[2 + i]);\n"
        "\t\tSAMPRakNet::GetCore()->logLn(LogLevel::Warning, \"unknown ID_AUTH_KEY challenge (%u bytes): %s\", challengeLen, hex);\n"
        "\t\treturn;\n"
        "\t}\n"
        "\n"
        "\tRakNet::BitStream bitStream;\n"
        "\tbitStream.Write<unsigned char>(ID_AUTH_KEY);\n"
        "\tbitStream.Write<unsigned char>((unsigned char)responseLen);\n"
        "\tbitStream.Write(response, (unsigned int)responseLen);\n"
        "\tSendImmediate((char*)bitStream.GetData(), bitStream.GetNumberOfBitsUsed(), SYSTEM_PRIORITY, RELIABLE, 0, playerId, false, false, RakNet::GetTime());\n"
        "}\n\n"
        "bool RakPeer::ParseConnectionAuthPacket(",
        "HandleAuthKeyChallenge impl",
    )

    # route ID_AUTH_KEY (client role) to the new handler
    t = sub_once(
        t,
        "\t\t\t\t\t\t// However, if we are connected we still take a connection request in case both systems are trying to connect to each other\n"
        "\t\t\t\t\t\t// at the same time\n"
        "\t\t\t\t\t\tif ( (unsigned char)(data)[0] == ID_CONNECTION_REQUEST )\n",
        "\t\t\t\t\t\t// FakeBots (client mode): the server challenges us after our connection request\n"
        "\t\t\t\t\t\tif ( (unsigned char)(data)[0] == ID_AUTH_KEY && remoteSystem->weInitiatedTheConnection )\n"
        "\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tHandleAuthKeyChallenge(remoteSystem, playerId, data, byteSize);\n"
        "\t\t\t\t\t\t\tdelete [] data;\n"
        "\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t// However, if we are connected we still take a connection request in case both systems are trying to connect to each other\n"
        "\t\t\t\t\t\t// at the same time\n"
        "\t\t\t\t\t\telse if ( (unsigned char)(data)[0] == ID_CONNECTION_REQUEST )\n",
        "route ID_AUTH_KEY",
    )

    # SERVER rule inside the shared peer: "kick whoever did not log on within 30 s".
    # A client never gets the server-side isLogon flag, so every bot used to
    # disconnect itself exactly 30 seconds after connecting.
    t = sub_once(
        t,
        "\t\t\t\t\tif (!remoteSystem->isLogon)\n",
        "\t\t\t\t\t// FakeBots (client mode): this is a server-side rule; a client must never apply it to its server.\n"
        "\t\t\t\t\tif (!remoteSystem->isLogon && !remoteSystem->weInitiatedTheConnection)\n",
        "logon timeout is server only",
    )

    # say which failure condition fired when a link is declared lost
    t = sub_once(
        t,
        "\t\t\t\t\t// Failed.  Inform the user?\n",
        "\t\t\t\t\t// Failed.  Inform the user?\n"
        "\t\t\t\t\tSAMPRakNet::GetCore()->printLn(\"link failure: dead=%d mode=%d waitingAcks=%d dataWaiting=%d msSinceReliableSend=%d\", (int)remoteSystem->reliabilityLayer.IsDeadConnection(), (int)remoteSystem->connectMode, (int)remoteSystem->reliabilityLayer.AreAcksWaiting(), (int)remoteSystem->reliabilityLayer.IsDataWaiting(), (int)(timeMS - remoteSystem->lastReliableSend));\n",
        "link failure diagnostic",
    )

    # offline replies from the server: cookie / full / banned
    marker = "\t\t// We didn't check this datagram to see if it came from a connected system or not yet.\n" \
             "\t\t// Therefore, this datagram must be under 17 bits - otherwise it may be normal network traffic as the min size for a raknet send is 17 bits\n" \
             "\t\telse if ((unsigned char)(data)[0] == ID_OPEN_CONNECTION_REQUEST && length == sizeof(unsigned char)*3)\n"
    new_branches = (
        "\t\t// FakeBots (client mode): the server answered the open-connection request with a\n"
        "\t\t// cookie. Store it and repeat the request on the next update cycle.\n"
        "\t\telse if ((unsigned char)(data)[0] == ID_OPEN_CONNECTION_COOKIE && length == sizeof(unsigned char)+sizeof(unsigned short))\n"
        "\t\t{\n"
        "\t\t\tunsigned short cookie;\n"
        "\t\t\tmemcpy(&cookie, data + 1, sizeof(cookie));\n"
        "\t\t\tRakPeer::RequestedConnectionStruct *rcsFirst, *rcs;\n"
        "\t\t\trcsFirst = rakPeer->requestedConnectionList.ReadLock();\n"
        "\t\t\trcs = rcsFirst;\n"
        "\t\t\twhile (rcs)\n"
        "\t\t\t{\n"
        "\t\t\t\tif (rcs->playerId==playerId)\n"
        "\t\t\t\t{\n"
        "\t\t\t\t\trcs->cookie = cookie;\n"
        "\t\t\t\t\tSAMPRakNet::GetCore()->printLn(\"peer %p: cookie 0x%04x received (after %u requests)\", (void*)rakPeer, (unsigned)cookie, (unsigned)rcs->requestsMade);\n"
        "\t\t\t\t\trcs->nextRequestTime = 0;\n"
        "\t\t\t\t}\n"
        "\t\t\t\trcs = rakPeer->requestedConnectionList.ReadLock();\n"
        "\t\t\t}\n"
        "\t\t\tif (rcsFirst)\n"
        "\t\t\t\trakPeer->requestedConnectionList.CancelReadLock(rcsFirst);\n"
        "\t\t\treturn;\n"
        "\t\t}\n"
        "\t\t// FakeBots (client mode): the server refused the connection attempt outright\n"
        "\t\t// (no free slot / banned). Cancel the attempt and tell the user immediately instead\n"
        "\t\t// of retrying silently until the attempt limit is reached.\n"
        "\t\telse if (((unsigned char)(data)[0] == ID_NO_FREE_INCOMING_CONNECTIONS || (unsigned char)(data)[0] == ID_CONNECTION_BANNED) && length <= sizeof(unsigned char)*2)\n"
        "\t\t{\n"
        "\t\t\tRakPeer::RequestedConnectionStruct *rcsFirst, *rcs;\n"
        "\t\t\trcsFirst = rakPeer->requestedConnectionList.ReadLock();\n"
        "\t\t\trcs = rcsFirst;\n"
        "\t\t\tbool cancelled = false;\n"
        "\t\t\twhile (rcs)\n"
        "\t\t\t{\n"
        "\t\t\t\tif (rcs->actionToTake==RakPeer::RequestedConnectionStruct::CONNECT && rcs->playerId==playerId)\n"
        "\t\t\t\t{\n"
        "\t\t\t\t\tcancelled = true;\n"
        "\t\t\t\t\tif (rcs==rcsFirst)\n"
        "\t\t\t\t\t{\n"
        "\t\t\t\t\t\trakPeer->requestedConnectionList.ReadUnlock();\n"
        "\t\t\t\t\t\trcsFirst=rakPeer->requestedConnectionList.ReadLock();\n"
        "\t\t\t\t\t\trcs=rcsFirst;\n"
        "\t\t\t\t\t}\n"
        "\t\t\t\t\telse\n"
        "\t\t\t\t\t{\n"
        "\t\t\t\t\t\trcs->playerId=UNASSIGNED_PLAYER_ID;\n"
        "\t\t\t\t\t\trcs=rakPeer->requestedConnectionList.ReadLock();\n"
        "\t\t\t\t\t}\n"
        "\t\t\t\t\tcontinue;\n"
        "\t\t\t\t}\n"
        "\t\t\t\trcs=rakPeer->requestedConnectionList.ReadLock();\n"
        "\t\t\t}\n"
        "\t\t\tif (rcsFirst)\n"
        "\t\t\t\trakPeer->requestedConnectionList.CancelReadLock(rcsFirst);\n"
        "\t\t\tif (cancelled)\n"
        "\t\t\t{\n"
        "\t\t\t\tpacket=AllocPacket(sizeof( char ));\n"
        "\t\t\t\tpacket->data[ 0 ] = data[0];\n"
        "\t\t\t\tpacket->bitSize = ( sizeof( char ) * 8);\n"
        "\t\t\t\tpacket->playerId = playerId;\n"
        "\t\t\t\tpacket->playerIndex = 65535;\n"
        "\t\t\t\trakPeer->AddPacketToProducer(packet);\n"
        "\t\t\t}\n"
        "\t\t\treturn;\n"
        "\t\t}\n"
    )
    t = sub_once(t, marker, new_branches + marker, "offline cookie/full/banned branches")
    return t


def patch_reliabilitylayer(t):
    # Say *why* a link died instead of failing silently.
    t = sub_once(
        t,
        "\t\t// We've waited a very long time for a reliable packet to get an ack and it never has\n\t\tdeadConnection = true;\n",
        "\t\t// We've waited a very long time for a reliable packet to get an ack and it never has\n"
        "\t\tSAMPRakNet::GetCore()->printLn(\"link dead: %u reliable message(s) unacknowledged for %lld ms (timeout %u ms)\", (unsigned)resendList.Size(), (long long)((time - lastAckTime) / 1000), (unsigned)timeoutTime);\n"
        "\t\tdeadConnection = true;\n",
        "dead connection diagnostic",
    )
    return t


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    for sub in ("Source", "Include/raknet"):
        (dst / sub).mkdir(parents=True, exist_ok=True)
    for f in (src / "Source").glob("*.cpp"):
        shutil.copy2(f, dst / "Source" / f.name)
    for f in (src / "Include/raknet").glob("*.h"):
        shutil.copy2(f, dst / "Include/raknet" / f.name)

    p = dst / "Source/SocketLayer.cpp"
    p.write_text(patch_socketlayer_reuse(patch_socketlayer(p.read_text(encoding="utf-8", errors="surrogateescape"))), encoding="utf-8", errors="surrogateescape")
    p = dst / "Include/raknet/SingleProducerConsumer.h"
    p.write_text(patch_singleproducerconsumer_h(p.read_text(encoding="utf-8", errors="surrogateescape")), encoding="utf-8", errors="surrogateescape")
    p = dst / "Include/raknet/SocketLayer.h"
    p.write_text(patch_socketlayer_h(p.read_text(encoding="utf-8", errors="surrogateescape")), encoding="utf-8", errors="surrogateescape")
    p = dst / "Include/raknet/RakPeer.h"
    p.write_text(patch_rakpeer_h(p.read_text(encoding="utf-8", errors="surrogateescape")), encoding="utf-8", errors="surrogateescape")
    p = dst / "Source/ReliabilityLayer.cpp"
    p.write_text(patch_reliabilitylayer(p.read_text(encoding="utf-8", errors="surrogateescape")), encoding="utf-8", errors="surrogateescape")
    p = dst / "Source/RakPeer.cpp"
    p.write_text(patch_rakpeer_cpp(p.read_text(encoding="utf-8", errors="surrogateescape")), encoding="utf-8", errors="surrogateescape")

    for f in ("Include/raknet/RakServerInterface.h", "Include/raknet/RakServer.h", "Include/raknet/RakPeerInterface.h"):
        q = dst / f
        s = q.read_text(encoding="utf-8", errors="surrogateescape")
        q.write_text(s.replace("(e.g., NPCs)", "(e.g., internal use)"), encoding="utf-8", errors="surrogateescape")
    print("patched ->", dst)


if __name__ == "__main__":
    main()
