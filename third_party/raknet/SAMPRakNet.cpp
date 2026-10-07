// FakeBots - client-mode SAMPRakNet implementation. See SAMPRakNet.hpp.
#include "SAMPRakNet.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

// Generated from the upstream SAMPRakNet.cpp by tools/gen_crypt_tables.py
//   kSampDecryptKey[256]  : server-side decode table (a bijection)
//   kSampAuthTable[256]   : { challenge, response } pairs of SA-MP servers
//   kOmpAuthTable[256]    : { challenge, response } pairs of open.mp servers
#include "SampCryptData.inc"

unsigned int SAMPRakNet::maxConnectionAttempts_ = 15;
unsigned int SAMPRakNet::timeout_ = 15000;
float SAMPRakNet::minimumSendBitsPerSecond_ = 96000.0f;

// ---------------------------------------------------------------------------
//  Datagram obfuscation (client -> server)
// ---------------------------------------------------------------------------
//  The server decodes a datagram like this (SAMPRakNet::Decrypt upstream):
//
//      mask = (uint8)(serverPort ^ 0xCC)
//      for i in 1 .. len-1:
//          cur = src[i]
//          if (i is even) cur ^= mask
//          cur = key[cur]
//          checksum ^= cur & 0xAA
//          plain[i-1] = cur
//      accept only if src[0] == checksum
//
//  so the client has to apply the inverse: invKey[], the same mask on the same
//  (even) positions and the identical checksum rule.
namespace
{
struct InverseKey
{
	unsigned char inv[256];
	InverseKey()
	{
		for (int i = 0; i < 256; ++i)
			inv[kSampDecryptKey[i]] = (unsigned char)i;
	}
};

const InverseKey& GetInverseKey()
{
	static const InverseKey k; // thread-safe initialisation (C++11)
	return k;
}
} // namespace

int SAMPRakNet::EncryptClientDatagram(const uint8_t* src, int len, uint16_t serverPort, uint8_t* dst, int dstCapacity)
{
	if (!src || !dst || len < 0 || dstCapacity < len + 1)
		return -1;

	const InverseKey& k = GetInverseKey();
	const uint8_t mask = (uint8_t)(serverPort ^ 0xCC);
	uint8_t checksum = 0;

	for (int i = 1; i <= len; ++i)
	{
		const uint8_t plain = src[i - 1];
		checksum ^= (uint8_t)(plain & 0xAA);
		uint8_t cur = k.inv[plain];
		if (!(i & 1))
			cur ^= mask;
		dst[i] = cur;
	}
	dst[0] = checksum;
	return len + 1;
}

// ---------------------------------------------------------------------------
//  ID_AUTH_KEY
// ---------------------------------------------------------------------------
bool SAMPRakNet::FindAuthResponse(const char* challenge, size_t challengeLen, const char** response, size_t* responseLen)
{
	if (!challenge || !response || !responseLen)
		return false;

	// The server sends size()+1 bytes, i.e. including the terminating '\0'.
	while (challengeLen > 0 && challenge[challengeLen - 1] == '\0')
		--challengeLen;
	if (challengeLen == 0)
		return false;

		// A SA-MP server challenges with an entry of its own table, an open.mp server with an
	// entry of its table (the two tables share no entry).
	const SampAuthEntry* const tables[2] = { kSampAuthTable, kOmpAuthTable };
	for (const SampAuthEntry* table : tables)
	{
		for (int i = 0; i < 256; ++i)
		{
			const char* c = table[i].challenge;
			if (std::strlen(c) == challengeLen && std::memcmp(c, challenge, challengeLen) == 0)
			{
				*response = table[i].response;
				*responseLen = std::strlen(table[i].response);
				return true;
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//  Thread-safe log queue
// ---------------------------------------------------------------------------
namespace
{
std::mutex g_logMutex;
std::vector<std::string> g_logQueue;
bool g_debugLogging = false;
RakLogSink g_sink;

void Enqueue(const char* prefix, const char* fmt, va_list ap)
{
	char buf[512];
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	std::string line(prefix);
	line += buf;
	std::lock_guard<std::mutex> lock(g_logMutex);
	if (g_logQueue.size() < 512) // never grow without bound
		g_logQueue.push_back(std::move(line));
}
} // namespace

void RakLogSink::printLn(const char* fmt, ...)
{
	if (!g_debugLogging)
		return;
	va_list ap;
	va_start(ap, fmt);
	Enqueue("[RakNet] ", fmt, ap);
	va_end(ap);
}

void RakLogSink::logLn(LogLevel level, const char* fmt, ...)
{
	if (level == LogLevel::Debug && !g_debugLogging)
		return;
	va_list ap;
	va_start(ap, fmt);
	Enqueue("[RakNet] ", fmt, ap);
	va_end(ap);
}

RakLogSink* SAMPRakNet::GetCore()
{
	return &g_sink;
}

size_t SAMPRakNet::DrainLog(std::string& out)
{
	std::vector<std::string> local;
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		local.swap(g_logQueue);
	}
	for (const std::string& s : local)
	{
		out += s;
		out += '\n';
	}
	return local.size();
}

void SAMPRakNet::SetDebugLogging(bool on)
{
	g_debugLogging = on;
}
