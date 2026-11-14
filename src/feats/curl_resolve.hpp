// SPDX-License-Identifier: AGPL-3.0-only
//
// Synchronous DNS pre-resolution for libcurl calls made during LD_AUDIT
// la_preinit(). ld.so holds GL(dl_load_lock) while running la_preinit(), so if
// libcurl's async DNS thread calls getaddrinfo() and triggers glibc NSS
// module_load() -> __libc_dlopen_mode(), the worker thread deadlocks waiting
// for GL(dl_load_lock) while curl_easy_cleanup() waits for the worker thread.
// Resolving on the calling thread and seeding CURLOPT_RESOLVE avoids both.

#pragma once

#include <curl/curl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace CurlResolve
{

struct ScopedResolveList
{
	using FreeFn = void (*)(curl_slist*);

	curl_slist* list = nullptr;
	FreeFn freeFn = nullptr;

	ScopedResolveList() = default;
	ScopedResolveList(const ScopedResolveList&) = delete;
	ScopedResolveList& operator=(const ScopedResolveList&) = delete;

	ScopedResolveList(ScopedResolveList&& other) noexcept
	    : list(other.list), freeFn(other.freeFn)
	{
		other.list = nullptr;
		other.freeFn = nullptr;
	}

	ScopedResolveList& operator=(ScopedResolveList&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			list = other.list;
			freeFn = other.freeFn;
			other.list = nullptr;
			other.freeFn = nullptr;
		}
		return *this;
	}

	~ScopedResolveList() { reset(); }

	void reset()
	{
		if (list && freeFn)
		{
			freeFn(list);
		}
		list = nullptr;
		freeFn = nullptr;
	}
};

inline bool parseUrlHostPort(const std::string& url,
                             std::string& hostOut,
                             std::string& portOut)
{
	const size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return false;
	const std::string scheme = url.substr(0, schemeEnd);
	std::string defaultPort;
	if (scheme == "https") defaultPort = "443";
	else if (scheme == "http") defaultPort = "80";
	else return false;

	const size_t authStart = schemeEnd + 3;
	const size_t authEnd = url.find_first_of("/?#", authStart);
	std::string authority = authEnd == std::string::npos
	    ? url.substr(authStart)
	    : url.substr(authStart, authEnd - authStart);

	const size_t atPos = authority.rfind('@');
	if (atPos != std::string::npos) authority.erase(0, atPos + 1);
	if (authority.empty()) return false;

	if (authority.front() == '[')
	{
		const size_t closeBracket = authority.find(']');
		if (closeBracket == std::string::npos) return false;
		hostOut = authority.substr(1, closeBracket - 1);
		if (closeBracket + 1 < authority.size() && authority[closeBracket + 1] == ':')
		{
			portOut = authority.substr(closeBracket + 2);
		}
		else
		{
			portOut = defaultPort;
		}
	}
	else
	{
		const size_t colon = authority.rfind(':');
		if (colon != std::string::npos)
		{
			hostOut = authority.substr(0, colon);
			portOut = authority.substr(colon + 1);
		}
		else
		{
			hostOut = authority;
			portOut = defaultPort;
		}
	}
	return !hostOut.empty() && !portOut.empty();
}

template <typename SetoptFn, typename SlistAppendFn, typename SlistFreeFn>
inline ScopedResolveList seedResolveForUrl(CURL* curl,
                                             const std::string& url,
                                             SetoptFn setoptFn,
                                             SlistAppendFn slistAppendFn,
                                             SlistFreeFn slistFreeFn)
{
	ScopedResolveList guard;
	if (!curl || !setoptFn || !slistAppendFn || !slistFreeFn) return guard;

	std::string host;
	std::string port;
	if (!parseUrlHostPort(url, host, port)) return guard;

	struct addrinfo hints {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
	{
		if (res) freeaddrinfo(res);
		return guard;
	}

	std::vector<std::string> addrs;
	for (const struct addrinfo* p = res; p != nullptr; p = p->ai_next)
	{
		char ipBuf[NI_MAXHOST] {};
		if (getnameinfo(p->ai_addr, p->ai_addrlen, ipBuf, sizeof(ipBuf),
		                nullptr, 0, NI_NUMERICHOST) == 0)
		{
			std::string ip(ipBuf);
			if (std::find(addrs.begin(), addrs.end(), ip) == addrs.end())
			{
				addrs.push_back(std::move(ip));
			}
		}
	}
	freeaddrinfo(res);

	if (addrs.empty()) return guard;

	std::string entry = host + ":" + port + ":";
	for (size_t i = 0; i < addrs.size(); ++i)
	{
		if (i > 0) entry += ',';
		entry += addrs[i];
	}

	curl_slist* list = slistAppendFn(nullptr, entry.c_str());
	if (!list) return guard;

	if (setoptFn(curl, CURLOPT_RESOLVE, list) != CURLE_OK)
	{
		slistFreeFn(list);
		return guard;
	}

	guard.list = list;
	guard.freeFn = slistFreeFn;
	return guard;
}

} // namespace CurlResolve

