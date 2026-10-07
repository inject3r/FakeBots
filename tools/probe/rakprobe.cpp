// rakprobe - stand-alone diagnostic: connects ONE bot
// to the server on 127.0.0.1 and prints the whole join handshake and every RPC the
// server sends. Useful to verify the RakNet layer against a real SA-MP / open.mp
// server without loading the plugin.
//
//   rakprobe [name] [port] [seconds] [local-bind-address]
//
// The optional bind address makes the probe look like a player from ANOTHER ip
// (e.g. 127.0.0.2): a real player never shares the address of the bots.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
#include "RakClientInterface.h"
#include "RakClient.h"
#include "RakNetworkFactory.h"
#include "BitStream.h"
#include "PacketEnumerations.h"
#include "GetTime.h"
#include "RakSleep.h"
#include "SAMPRakNet.hpp"
#include "RakPeer.h"

using namespace RakNet;

static std::string hexMul1001(const std::string& hex)
{
	// returns hex * 1001 (the server insists that the key is divisible by 1001)
	std::string out;
	unsigned carry = 0;
	for (int i = (int)hex.size() - 1; i >= 0; --i)
	{
		char ch = hex[i];
		unsigned d = (ch >= 'A') ? (ch - 'A' + 10) : (ch - '0');
		unsigned v = d * 1001u + carry;
		out.push_back("0123456789ABCDEF"[v & 15]);
		carry = v >> 4;
	}
	while (carry) { out.push_back("0123456789ABCDEF"[carry & 15]); carry >>= 4; }
	return std::string(out.rbegin(), out.rend());
}

static bool g_initGame = false;
static RakClientInterface* g_client = 0;
static bool g_spawned = false;
static void OnRpc(RPCParameters* p, void* extra)
{
	const unsigned id = (unsigned)(size_t)extra;
	printf("  [rpc] id=%u bits=%u", id, p->numberOfBitsOfData);
	if (id == 129 || id == 128)
	{
		printf("  payload:");
		for (unsigned i = 0; i < (p->numberOfBitsOfData + 7) / 8 && i < 40; ++i)
			printf(" %02x", p->input[i]);
	}
	printf("\n");
	if (id == 139)
	{
		g_initGame = true;
		BitStream bs;
		bs.Write<int32_t>(0);
		g_client->RPC(128, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, false, UNASSIGNED_NETWORK_ID, 0);
		printf("  -> RequestClass(0)\n");
	}
	else if (id == 128)
	{
		BitStream bs;
		g_client->RPC(129, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, false, UNASSIGNED_NETWORK_ID, 0);
		printf("  -> RequestSpawn\n");
	}
	else if (id == 129)
	{
		BitStream bs;
		g_client->RPC(52, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, false, UNASSIGNED_NETWORK_ID, 0);
		printf("  -> Spawn\n");
		g_spawned = true;
	}
}

int main(int argc, char** argv)
{
	const char* name = argc > 1 ? argv[1] : "Probe_Bot";
	unsigned short port = argc > 2 ? (unsigned short)atoi(argv[2]) : 7777;
	int seconds = argc > 3 ? atoi(argv[3]) : 12;
	srand((unsigned)time(0));
	SAMPRakNet::SetDebugLogging(true);

	RakClientInterface* c = RakNetworkFactory::GetRakClientInterface();
	g_client = c;
	for (int id = 0; id < 256; ++id)
		c->RegisterAsRemoteProcedureCall((RPCID)id, OnRpc, (void*)(size_t)id);

	const char* bindAddr = argc > 4 ? argv[4] : 0;
	if (bindAddr)
	{
		// RakClient::Connect() always binds to the default address; use the peer API so the
		// socket can be bound to a specific local address.
		RakPeer* peer = static_cast<RakPeer*>(static_cast<RakClient*>(c));
		peer->RakPeer::Disconnect(100);
		if (!peer->RakPeer::Initialize(1, 0, 10, bindAddr)) { printf("Initialize(%s) failed\n", bindAddr); return 1; }
		if (!peer->RakPeer::Connect("127.0.0.1", port, 0, 0)) { printf("Connect() failed\n"); return 1; }
	}
	else if (!c->Connect("127.0.0.1", port, 0, 0, 10)) { printf("Connect() failed\n"); return 1; }
	printf("connecting to 127.0.0.1:%u as %s ...\n", port, name);

	unsigned t0 = RakNet::GetTime();
	bool joined = false;
	while (RakNet::GetTime() - t0 < (unsigned)seconds * 1000)
	{
		Packet* p;
		while ((p = c->Receive()) != 0)
		{
			unsigned char id = p->data[0];
			printf("[pkt] id=%u len=%u\n", id, p->length);
			if (id == ID_CONNECTION_REQUEST_ACCEPTED && p->length >= 13)
			{
				uint32_t token; memcpy(&token, p->data + 9, 4);
				uint16_t idx;   memcpy(&idx, p->data + 7, 2);
				printf("  accepted: playerIndex=%u token=0x%08x\n", idx, token);
				std::string x; for (int i = 0; i < 40; ++i) x.push_back("0123456789ABCDEF"[(i == 0) ? 1 + rand() % 15 : rand() % 16]);
				std::string key = hexMul1001(x);
				BitStream bs;
				bs.Write<uint32_t>(4057);
				bs.Write<uint8_t>(1);
				bs.Write<uint8_t>((uint8_t)strlen(name)); bs.Write(name, (unsigned)strlen(name));
				bs.Write<uint32_t>(token ^ 4057u);
				bs.Write<uint8_t>((uint8_t)key.size()); bs.Write(key.data(), (unsigned)key.size());
				const char* ver = "0.3.7-R2";
				bs.Write<uint8_t>((uint8_t)strlen(ver)); bs.Write(ver, (unsigned)strlen(ver));
				c->RPC(25, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 2, false, UNASSIGNED_NETWORK_ID, 0);
				printf("  -> sent PlayerConnect RPC (25), key=%s\n", key.c_str());
				joined = true;
			}
			c->DeallocatePacket(p);
		}
		std::string log; if (SAMPRakNet::DrainLog(log)) fputs(log.c_str(), stdout);
		RakSleep(5);
	}
	printf("RESULT joined=%d initgame=%d spawned=%d connected=%d\n", joined ? 1 : 0, g_initGame ? 1 : 0, g_spawned ? 1 : 0, c->IsConnected() ? 1 : 0);
	c->Disconnect(300);
	RakNetworkFactory::DestroyRakClientInterface(c);
	return 0;
}
