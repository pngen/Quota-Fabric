#pragma once
// Portable TCP transport (Winsock on Windows, POSIX on other platforms).
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <climits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET qf_socket_t;
#define QF_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
typedef int qf_socket_t;
#define QF_INVALID_SOCKET (-1)
#endif

namespace quota_fabric {
void net_startup();
void net_cleanup();
struct Endpoint { std::string host = "127.0.0.1"; std::uint16_t port = 0; };

class TcpSocket {
 public:
  TcpSocket() = default;
  explicit TcpSocket(qf_socket_t s) : fd_(s) {}
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  TcpSocket(TcpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = QF_INVALID_SOCKET; }
  TcpSocket& operator=(TcpSocket&& o) noexcept;
  ~TcpSocket() { close(); }
  bool valid() const noexcept { return fd_ != QF_INVALID_SOCKET; }
  qf_socket_t native() const noexcept { return fd_; }
  static std::optional<TcpSocket> connect(const Endpoint& ep, std::string* err);
  static std::optional<TcpSocket> listen(const Endpoint& ep, std::string* err);
  std::optional<TcpSocket> accept(std::string* err);
  bool send_all(const std::uint8_t* data, std::size_t n);
  bool recv_all(std::uint8_t* data, std::size_t n);
  void shutdown_send();
  void close();
 private:
  static void close_impl(qf_socket_t s);
  qf_socket_t fd_ = QF_INVALID_SOCKET;
};
}  // namespace quota_fabric
