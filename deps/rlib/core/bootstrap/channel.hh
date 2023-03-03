#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "../common.hh"

#include "../utils/ipname.hh"
#include "../utils/marshal.hh"

namespace rdmaio {

namespace bootstrap {

const usize kMaxMsgSz = 4096;

class AbsChannel {
protected:
  int sock_fd = -1;

  explicit AbsChannel(int sock) : sock_fd(sock) {}

  AbsChannel() : sock_fd(-1) {}

  bool valid() const { return sock_fd >= 0; }

  // possible to set later
  void set_socket(int fd)  {
    sock_fd = fd;
  }

  Result<> close_channel() {
    if (valid()) {
      close(sock_fd);
      sock_fd = -1;
    }
    return Ok(); // we donot check the result here
  }

  template <typename SockAddr>
  Result<std::string> raw_send(const ByteBuffer &buf, const SockAddr &addr) {
    RDMA_ASSERT(valid());
    const struct sockaddr *addr_p =
        reinterpret_cast<const struct sockaddr *>(&addr);

    if (sendto(sock_fd, buf.c_str(), buf.size(), MSG_CONFIRM, addr_p,
               sizeof(addr)) <= 0) {
      return Err(std::string(strerror(errno)));
    }
    return Ok(std::string(""));
  }

  /*!
    \param timeout: timeout in usec
   */
  Result<sockaddr> try_recv(ByteBuffer &buf, const double to_usec = 1000000) {
    struct sockaddr addr;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_fd, &rfds);
    struct timeval tv = {.tv_sec = 0, .tv_usec = static_cast<int>(to_usec)};

    auto ready = select(sock_fd + 1, &rfds, nullptr, nullptr, &tv);

    switch (ready) {
    case 0:
      return Timeout(addr);
    case -1:
      return Err(addr);
    default: {
      if (FD_ISSET(sock_fd, &rfds)) {
        // now recv the msg
        usize len = sizeof(addr);
        auto n = recvfrom(sock_fd, (char *)(buf.c_str()), buf.size(), 0,
                          (struct sockaddr *)(&addr), &len);

        // we successfully receive one msg
        if (n >= 0) {
          return Ok(addr);
        }
        else {
          //RDMA_LOG(4) << "error: " << strerror(errno);
          return Err(addr);
        }
      } else {
        RDMA_ASSERT(false);
      }
    }
      // end switch
    }
    return Err(addr); // should not return here
  }

public:
  // It has to be abstract, otherwise shared_ptr cannot deallocate it
  ~AbsChannel() { close_channel(); }
};

/*!
  A UDP-based channel for sending msgs and recv msgs.
  Each msg is at maxinum ::rdmaio::bootstrap::kMaxMsgSz .
  An example:
  `
  auto sc = SendChannel::create("xx.xx.xx.xx:xx").value();
  auto send_res = sc->send("hello");
  ASSERT(send_res == IOCode::Ok);
  // ... wait some time
  auto recv_res = sc->recv(1000); // recv with 1 second timeout
  ASSERT(recv_res == IOCode::Ok);
  `
 */
class SendChannel : public AbsChannel {

  struct sockaddr_in end_addr;

  explicit SendChannel(const std::string &ip, int port) :
        end_addr(convert_addr(ip, port)) {
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &servinfo)) != 0) {
      return;
    }

    int sockfd = -1;
    // loop through all the results and make a socket
    for (auto p = servinfo; p != NULL; p = p->ai_next) {
      if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
          -1) {
        continue;
      }

      break;
    }

    this->set_socket(sockfd);

    if (valid()) {
      // set as a non-blocking channel
      fcntl(this->sock_fd, F_SETFL, O_NONBLOCK);
    } else {
      RDMA_LOG(4) << "failed to create send socket to: " << ip << ":" << port;
    }
  }

  static sockaddr_in convert_addr(const std::string &ip, int port) {
    return {.sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = {
                .s_addr = inet_addr(ip.c_str()),
            }};
    //
  }

public:
  /*!
  Create a msg for the remote.
  The address is in the format (ip:port)
 */
  static Option<Arc<SendChannel>> create(const std::string &addr) {
    auto host_port = IPNameHelper::parse_addr(addr);
    if (host_port) {
      auto ip_res = IPNameHelper::host2ip(std::get<0>(host_port.value()));
      if (ip_res == IOCode::Ok) {
        auto sc = Arc<SendChannel>(
            new SendChannel(ip_res.desc, std::get<1>(host_port.value())));
        if (sc->valid())
          return sc;
      }
    }
    return {};
  }

  /*!
    Send a buffer to this channel using the already connect address `end_addr`
   */
  Result<std::string> send(const ByteBuffer &buf) {
    return raw_send(buf, end_addr);
  }

  /*!
    Recv a reply of on the channel
   */
  Result<ByteBuffer> recv(const double &timeout_usec = 1000) {
    ByteBuffer buf(kMaxMsgSz, '\0');
    auto res = try_recv(buf, timeout_usec);
    if (res == IOCode::Ok) {
      return Ok(std::move(buf));
    }
    // direct forward the res code to the reply
    return Result<ByteBuffer>({.code = res.code, .desc = ByteBuffer("")});
  }
};

/*!
  A UDP-based channel for recving msgs.
  Each msg is at maxinum ::rdmaio::bootstrap::kMaxMsgSz .
  To use:
  `
  auto rc = RecvChannel::create (port_to_listen).value();
  for (rc->start(); rc->has_msg();rc->next()) {
    auto &msg = rc->cur();
    ...
    rc->reply_cur( some_reply);
  }
  `
 */
class RecvChannel : public AbsChannel {

  Option<sockaddr> cur_msg_client = {}; // who sent the current client
  ByteBuffer cur_msg;

  explicit RecvChannel(int port)
      : cur_msg(::rdmaio::bootstrap::kMaxMsgSz, '\0') {
    struct addrinfo hints, *servinfo, *p;
    socklen_t addr_len;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    int sockfd = -1;

    if ((getaddrinfo(nullptr, std::to_string(port).c_str(), &hints,
                     &servinfo)) != 0) {
      goto err;
    }

    char host[256];
    for (p = servinfo; p != NULL; p = p->ai_next) {

      if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
          -1) {
        continue;
      }
      getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0,
                  NI_NUMERICHOST);
      if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
        close(sockfd);
        continue;
      }
      //RDMA_LOG(2) << "choose host : " << host;

      break;
    }
    if (p == nullptr)
      RDMA_LOG(4) << "failed to bind port: " << port;
  err:
    set_socket(sockfd);
    freeaddrinfo(servinfo);

    if (valid()) {
      fcntl(this->sock_fd, F_SETFL, O_NONBLOCK);
    }
  }

  explicit RecvChannel(int port, const std::string &host)
    : AbsChannel(socket(AF_INET, SOCK_DGRAM, 0)), cur_msg(kMaxMsgSz, '\0') {
    if (valid()) {
      // set as a non-blocking channel
      fcntl(this->sock_fd, F_SETFL, O_NONBLOCK);

      // bind to this port
      sockaddr_in my_addr = {
          .sin_family = AF_INET,
          .sin_port = htons(port),
          .sin_addr = {.s_addr =
                           (host == "localhost" ? INADDR_ANY
                                                : inet_addr(host.c_str()))}};
      if (bind(this->sock_fd, (const struct sockaddr *)&my_addr,
               sizeof(my_addr))) {
        RDMA_LOG(4) << "bind to port: " << port
                    << " error with error: " << strerror(errno);
        close_channel();
      }
    }
  }

public:
  static Option<Arc<RecvChannel>> create(int port,const std::string &h = "localhost") {
    auto rc = Arc<RecvChannel>(new RecvChannel(port));
    if (rc->valid()) {
      return rc;
    }
    return {};
  }

  /*!
    Try recv a msg;
    \param timeout: in usec
    */
  void start(const double &timeout_usec = 1000) {
    // donot over consume the current msg
    if (has_msg())
      return;
    auto res = try_recv(cur_msg, timeout_usec);
    if (res == IOCode::Ok) {
      cur_msg_client = res.desc;
    }
  }

  bool has_msg() const {
    if (cur_msg_client)
      return true;
    return false;
  }

  /*!
    Drop current msg, and try to recv another one
   */
  void next() {
    cur_msg_client = {}; // re-set msg header
    start();             // fill one msg
  }

  /*!
    \note: this call is not safe
   */
  ByteBuffer &cur() { return cur_msg; }

  Result<std::string> reply_cur(const ByteBuffer &buf) {
    return raw_send(buf, cur_msg_client.value());
  };
}; // namespace bootstrap

} // namespace bootstrap

} // namespace rdmaio
