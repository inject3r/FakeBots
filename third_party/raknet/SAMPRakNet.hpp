// ============================================================================
//  FakeBots  -  client-mode replacement for "SAMPRakNet.hpp"
// ============================================================================
//  The upstream openmultiplayer/RakNet fork ships a *server* oriented
//  SAMPRakNet that depends on the open.mp SDK (ICore, Query, ...). That header
//  cannot even be compiled outside of the open.mp source tree, and its crypto
//  runs in the wrong direction for a client.
//
//  This file keeps the exact same name and the exact same symbols the RakNet
//  sources use, but implements the *client side* of the SA-MP / open.mp wire
//  protocol:
//     * datagram obfuscation (client -> server)  : EncryptClientDatagram()
//     * connection cookie                        : COOKIE_XOR
//     * ID_AUTH_KEY challenge/response           : FindAuthResponse()
//  Everything that only makes sense for a server is a harmless stub.
//
//  Thread-safety: every function below is safe to call from many bot threads
//  at the same time (no shared scratch buffers).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
// The RakNet sources use the Win32 spellings unconditionally.
#ifndef __stdcall
#define __stdcall
#endif
typedef int SOCKET; // identical to the typedef in SocketLayer.h (a repeated identical typedef is legal)
#endif

#include "Include/raknet/MTUSize.h"
#include "Include/raknet/NetworkTypes.h"
#include "Include/raknet/GetTime.h"

#define MAX_AUTH_RESPONSE_LEN (64)
#define MAX_UNVERIFIED_RPCS (5)
#define LOCALHOST (0x0100007fu)

// Only the levels the RakNet sources use.
enum class LogLevel
{
	Debug,
	Message,
	Warning,
	Error
};

// Tiny non-owning string view (the upstream code uses the open.mp SDK one).
struct StringView
{
	const char* ptr_;
	size_t len_;

	StringView(const char* p = nullptr, size_t l = 0)
		: ptr_(p)
		, len_(l)
	{
	}
	const char* data() const { return ptr_; }
	size_t size() const { return len_; }
	bool operator==(const StringView& o) const
	{
		return len_ == o.len_ && (len_ == 0 || std::memcmp(ptr_, o.ptr_, len_) == 0);
	}
};

// Thread-safe sink used by every RakNet log call. Messages are queued and the
// plugin drains them from the server thread (the server logger is not
// thread-safe, and RakNet logs from the per-bot network threads).
class RakLogSink
{
public:
	void printLn(const char* fmt, ...)
#if defined(__GNUC__)
		__attribute__((format(printf, 2, 3)))
#endif
		;
	void logLn(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
		__attribute__((format(printf, 3, 4)))
#endif
		;
};

class SAMPRakNet
{
public:
	// The cookie the server hands out must be XORed with this constant
	// (0x6969 is the original SA-MP value; the open.mp server accepts it).
	static constexpr uint16_t COOKIE_XOR = 0x6969;

	enum AuthType
	{
		AuthType_Invalid,
		AuthType_Player
	};

	struct RemoteSystemData
	{
		uint8_t authIndex;
		AuthType authType;
		uint8_t unverifiedRPCs;

		RemoteSystemData()
			: authIndex(0)
			, authType(AuthType_Invalid)
			, unverifiedRPCs(0)
		{
		}
	};

	// ---------------------------------------------------------------------
	//  Client protocol helpers
	// ---------------------------------------------------------------------

	// Obfuscates one outgoing datagram exactly like a real SA-MP client does.
	//   dst[0]   = checksum
	//   dst[1..] = substitution-table encoded payload (port dependent mask on
	//              every second byte)
	// `serverPort` is the UDP port of the server we are talking to.
	// Returns the number of bytes written to dst (len + 1) or -1 on error.
	static int EncryptClientDatagram(const uint8_t* src, int len, uint16_t serverPort, uint8_t* dst, int dstCapacity);

	// Looks up the ID_AUTH_KEY response for a challenge string sent by the
	// server. `challenge` may or may not contain the trailing '\0'.
	static bool FindAuthResponse(const char* challenge, size_t challengeLen, const char** response, size_t* responseLen);

	// How many times an ID_OPEN_CONNECTION_REQUEST is (re)sent before the
	// attempt is reported as ID_CONNECTION_ATTEMPT_FAILED (1 per second).
	static unsigned int GetMaxConnectionAttempts() { return maxConnectionAttempts_; }
	static void SetMaxConnectionAttempts(unsigned int n) { maxConnectionAttempts_ = n ? n : 1; }

	// Reliability layer timeout (ms without an ack before the link is dead).
	static unsigned int GetTimeout() { return timeout_; }
	static void SetTimeout(unsigned int ms) { timeout_ = ms; }

	// Congestion-control floor/ceiling used by the reliability layer.
	static float GetMinimumSendBitsPerSecond() { return minimumSendBitsPerSecond_; }
	static void SetMinimumSendBitsPerSecond(float bps) { minimumSendBitsPerSecond_ = bps; }

	// The per-second limits the *server* applies to its clients make no sense
	// for a client reading server traffic; they are effectively disabled.
	static unsigned int GetMessagesLimit() { return UINT_MAX; }
	static unsigned int GetMessageHoleLimit() { return UINT_MAX; }
	static unsigned int GetAcksLimit() { return UINT_MAX; }
	static unsigned int GetNetworkLimitsBanTime() { return 0; }
	static RakNet::RakNetTime GetGracePeriod() { return UINT_MAX; }

	// Log sink (see RakLogSink). Drained by the plugin on the server thread.
	static RakLogSink* GetCore();
	static size_t DrainLog(std::string& out); // appends queued lines, returns count
	static void SetDebugLogging(bool on);

	// ---------------------------------------------------------------------
	//  Server-role stubs. RakPeer still contains the server code paths; they
	//  are never reached by a client but must link.
	// ---------------------------------------------------------------------
	static uint32_t GetToken() { return 0; }
	static std::pair<uint8_t, StringView> GenerateAuth() { return std::pair<uint8_t, StringView>(0, StringView()); }
	static bool CheckAuth(uint8_t, StringView) { return false; }
	static bool IsOmpEncryptionEnabled() { return false; }
	static void SetRequestingConnection(unsigned int, bool) { }
	static void ResetOmpPlayerConfiguration(const RakNet::PlayerID&) { }
	static bool OnConnectionRequest(SOCKET, RakNet::PlayerID&, const char*, RakNet::RakNetTime&, RakNet::RakNetTime&) { return false; }

private:
	static unsigned int maxConnectionAttempts_;
	static unsigned int timeout_;
	static float minimumSendBitsPerSecond_;
};
