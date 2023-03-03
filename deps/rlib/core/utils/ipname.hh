#pragma once

#include <mutex>
#include <map>

// net related
#include <netdb.h>

#include "../common.hh"

#include "./timer.hh"

namespace rdmaio {

class IPNameHelper {
public:
  /*!
    given a "host:port", return (host,port) if parsed success
   */
  static Option<std::pair<std::string, int>> parse_addr(const std::string &h) {
    auto pos = h.find(':');
    if (pos != std::string::npos) {
      std::string host_str = h.substr(0, pos);
      std::string port_str = h.substr(pos + 1);

      std::stringstream parser(port_str);

      int port = 0;
      if (parser >> port) {
        return std::make_pair(host_str, port);
      }
    }
    return {};
  }

  // XD: fixme, do we need a global lock here?
  static std::string inet_ntoa(struct in_addr addr) {
    return std::string(inet_ntoa(addr));
  }

  /*!
    Given a host, return the ip associated with this host
    To accelerate the pass, we use a cache to store the recently queried
    host/ips

    \note This function is very slow, it's main objective is to bootstrap QP
    connection (finding addresses)

    \ret Ok -> res.desc = parsed IP
         Err -> res.desc = Err code
    */
  static Result<std::string> host2ip(const std::string &host) {
    static std::mutex lock;
    static std::map<std::string, std::pair<std::string, Timer>> ip_cache;
    const double cache_lease_sec = 10; // the cache will timeout per 10 second

    // trim the host
    auto trimed_host = host;
    trimed_host.erase(0, trimed_host.find_first_not_of(" "));
    trimed_host.erase(trimed_host.find_last_not_of(" ") + 1);

    {
      std::lock_guard<std::mutex> guard(lock);

      // 1. check if already cached and not timeout
      auto it = ip_cache.find(trimed_host);
      if (it != ip_cache.end()) {
        if (std::get<1>(it->second).passed_sec() < cache_lease_sec)
          return Ok(std::get<0>(it->second));
        else {
          // invalid the cache
          ip_cache.erase(it);
        }
      }

      // 2. really find the ip
      struct addrinfo hints, *infoptr;
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_INET; // AF_INET means IPv4 only addresses

      if (getaddrinfo(trimed_host.c_str(), nullptr, &hints, &infoptr)) {
        return Err(std::string(strerror(errno)));
      }
      char ip[64];
      memset(ip, 0, sizeof(ip));

      for (struct addrinfo *p = infoptr; p != nullptr; p = p->ai_next) {
        getnameinfo(p->ai_addr, p->ai_addrlen, ip, sizeof(ip), nullptr, 0,
                    NI_NUMERICHOST);
      }

      auto res = std::string(ip);
      if (res != "") {
        Timer t;
        ip_cache.insert(
            std::make_pair(trimed_host, std::make_pair(res, std::move(t))));
        return Ok(res);
      }
      return Err(std::string("not found"));
    }

    return Err(std::string("null"));
  }
};

} // namespace rdmaio
