#include "quota_fabric/protocol/net.hpp"
#include <cstring>
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

namespace quota_fabric {

#ifdef _WIN32
static bool g_wsock_init = false;
void net_startup() {
  if (!g_wsock_init) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_wsock_init = true;
  }
}
void net_cleanup() { if (g_wsock_init) { WSACleanup(); g_wsock_init = false; } }
#else
void net_startup() {}
void net_cleanup() {}
#endif

TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
  if (this != &o) { close(); fd_ = o.fd_; o.fd_ = QF_INVALID_SOCKET; }
  return *this;
}

std::optional<TcpSocket> TcpSocket::connect(const Endpoint& ep, std::string* err) {
  net_startup();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  const std::string port = std::to_string(ep.port);
  if (getaddrinfo(ep.host.c_str(), port.c_str(), &hints, &res) != 0) {
    if (err) *err = "getaddrinfo failed";
    return std::nullopt;
  }
  qf_socket_t s = QF_INVALID_SOCKET;
  for (auto* ai = res; ai; ai = ai->ai_next) {
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == QF_INVALID_SOCKET) continue;
    if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    s = QF_INVALID_SOCKET;
  }
  freeaddrinfo(res);
  if (s == QF_INVALID_SOCKET) { if (err) *err = "connect failed"; return std::nullopt; }
  return TcpSocket(s);
}

std::optional<TcpSocket> TcpSocket::listen(const Endpoint& ep, std::string* err) {
  net_startup();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_PASSIVE;
  const std::string port = std::to_string(ep.port);
  if (getaddrinfo(ep.host.c_str(), port.c_str(), &hints, &res) != 0) { if (err) *err = "getaddrinfo failed"; return std::nullopt; }
  qf_socket_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (s == QF_INVALID_SOCKET) { freeaddrinfo(res); if (err) *err = "socket failed"; return std::nullopt; }
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  if (bind(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    freeaddrinfo(res); if (err) *err = "bind failed"; return std::nullopt;
  }
  freeaddrinfo(res);
  if (::listen(s, 16) != 0) { if (err) *err = "listen failed"; close_impl(s); return std::nullopt; }
  return TcpSocket(s);
}
void TcpSocket::close_impl(qf_socket_t s) {
#ifdef _WIN32
  closesocket(s);
#else
  ::close(s);
#endif
}
std::optional<TcpSocket> TcpSocket::accept(std::string* err) {
  sockaddr_storage addr{}; int len = sizeof(addr);
#ifdef _WIN32
  qf_socket_t c = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
#else
  qf_socket_t c = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), reinterpret_cast<socklen_t*>(&len));
#endif
  if (c == QF_INVALID_SOCKET) { if (err) *err = "accept failed"; return std::nullopt; }
  // Ensure accepted socket is blocking (default) and not inheritable (Windows).
#ifdef _WIN32
  u_long mode = 0;
  ioctlsocket(c, FIONBIO, &mode);
  SetHandleInformation(reinterpret_cast<HANDLE>(c), HANDLE_FLAG_INHERIT, 0);
#endif
  return TcpSocket(c);
}

bool TcpSocket::send_all(const std::uint8_t* data, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
#ifdef _WIN32
    const int chunk = static_cast<int>(n - sent > INT_MAX ? INT_MAX : n - sent);
    const int r = ::send(fd_, reinterpret_cast<const char*>(data + sent), chunk, 0);
#else
    const ssize_t r = ::send(fd_, data + sent, n - sent, 0);
#endif
    if (r <= 0) return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}
bool TcpSocket::recv_all(std::uint8_t* data, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
#ifdef _WIN32
    const int chunk = static_cast<int>(n - got > INT_MAX ? INT_MAX : n - got);
    const int r = ::recv(fd_, reinterpret_cast<char*>(data + got), chunk, 0);
#else
    const ssize_t r = ::recv(fd_, data + got, n - got, 0);
#endif
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}
void TcpSocket::shutdown_send() {
#ifdef _WIN32
  ::shutdown(fd_, SD_SEND);
#else
  ::shutdown(fd_, SHUT_WR);
#endif
}
void TcpSocket::close() {
  if (fd_ != QF_INVALID_SOCKET) { close_impl(fd_); fd_ = QF_INVALID_SOCKET; }
}
}  // namespace quota_fabric
