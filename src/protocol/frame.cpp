#include "quota_fabric/protocol/frame.hpp"
#include <cstring>

namespace quota_fabric {

namespace {
struct BigWriter {  // big-endian
  void u32(std::uint32_t v) { b.push_back(std::uint8_t(v >> 24)); b.push_back(std::uint8_t(v >> 16)); b.push_back(std::uint8_t(v >> 8)); b.push_back(std::uint8_t(v)); }
  void u16(std::uint16_t v) { b.push_back(std::uint8_t(v >> 8)); b.push_back(std::uint8_t(v)); }
  void u64(std::uint64_t v) { for (int i = 7; i >= 0; --i) b.push_back(std::uint8_t(v >> (i * 8))); }
  void u8(std::uint8_t v) { b.push_back(v); }
  std::vector<std::uint8_t> b;
};
struct BigReader {
  const std::uint8_t* p; std::size_t n; std::size_t off = 0;
  bool u32(std::uint32_t* v) { if (off + 4 > n) return false; *v = (std::uint32_t(p[off]) << 24) | (std::uint32_t(p[off+1]) << 16) | (std::uint32_t(p[off+2]) << 8) | p[off+3]; off += 4; return true; }
  bool u16(std::uint16_t* v) { if (off + 2 > n) return false; *v = std::uint16_t((std::uint16_t(p[off]) << 8) | p[off+1]); off += 2; return true; }
  bool u64(std::uint64_t* v) { if (off + 8 > n) return false; std::uint64_t r = 0; for (int i = 7; i >= 0; --i) r = (r << 8) | p[off+i]; off += 8; *v = r; return true; }
  bool u8(std::uint8_t* v) { if (off + 1 > n) return false; *v = p[off++]; return true; }
};
}  // namespace

std::vector<std::uint8_t> encode_frame(MessageType type, std::uint64_t corr, std::uint8_t flags, const std::vector<std::uint8_t>& payload) {
  BigWriter w;
  w.u32(kFrameMagic); w.u16(kProtocolVersion); w.u8(static_cast<std::uint8_t>(type)); w.u8(flags);
  w.u64(corr); w.u32(static_cast<std::uint32_t>(payload.size()));
  w.b.insert(w.b.end(), payload.begin(), payload.end());
  return std::move(w.b);
}

bool send_frame(TcpSocket& sock, MessageType type, std::uint64_t corr, std::uint8_t flags, const std::vector<std::uint8_t>& payload, std::string* err) {
  auto frame = encode_frame(type, corr, flags, payload);
  if (!sock.send_all(frame.data(), frame.size())) { if (err) *err = "send_all failed"; return false; }
  return true;
}

bool validate_frame_header_magic(const std::uint8_t* h) {
  const std::uint32_t m = (std::uint32_t(h[0]) << 24) | (std::uint32_t(h[1]) << 16) | (std::uint32_t(h[2]) << 8) | h[3];
  return m == kFrameMagic;
}

std::optional<Frame> decode_frame(const std::uint8_t* data, std::size_t n, std::string* err) {
  if (n < 20) { if (err) *err = "frame too small"; return std::nullopt; }
  BigReader r{data, n};
  std::uint32_t magic = 0; r.u32(&magic);
  if (magic != kFrameMagic) { if (err) *err = "bad frame magic"; return std::nullopt; }
  std::uint16_t ver = 0; r.u16(&ver);
  if (ver != kProtocolVersion) { if (err) *err = "unsupported protocol version"; return std::nullopt; }
  std::uint8_t type = 0, flags = 0; r.u8(&type); r.u8(&flags);
  std::uint64_t corr = 0; r.u64(&corr);
  std::uint32_t plen = 0; r.u32(&plen);
  if (plen > kMaxFrameSize) { if (err) *err = "frame oversized"; return std::nullopt; }
  if (r.off + plen > n) { if (err) *err = "truncated frame"; return std::nullopt; }
  Frame f;
  f.type = static_cast<MessageType>(type); f.flags = flags; f.correlation = corr;
  f.payload.assign(data + r.off, data + r.off + plen);
  return f;
}

std::optional<Frame> recv_frame(TcpSocket& sock, std::uint32_t max_size, std::string* err) {
  std::uint8_t h[20];
  if (!sock.recv_all(h, 20)) { if (err) *err = "socket closed reading header"; return std::nullopt; }
  if (!validate_frame_header_magic(h)) { if (err) *err = "bad frame magic"; return std::nullopt; }
  const std::uint16_t version = std::uint16_t((std::uint16_t(h[4]) << 8) | h[5]);
  if (version != kProtocolVersion) { if (err) *err = "unsupported protocol version"; return std::nullopt; }
  const std::uint8_t type = h[6], flags = h[7];
  std::uint64_t corr = 0;
  for (int i = 0; i < 8; ++i) corr = (corr << 8) | h[8 + i];
  const std::uint32_t plen = (std::uint32_t(h[16]) << 24) | (std::uint32_t(h[17]) << 16) | (std::uint32_t(h[18]) << 8) | h[19];
  if (plen > kMaxFrameSize) { if (err) *err = "frame oversized"; return std::nullopt; }
  if (max_size && plen > max_size) { if (err) *err = "frame exceeds caller bound"; return std::nullopt; }
  Frame f;
  f.type = static_cast<MessageType>(type); f.flags = flags; f.correlation = corr;
  f.payload.resize(plen);
  if (plen > 0 && !sock.recv_all(f.payload.data(), plen)) { if (err) *err = "socket closed reading payload"; return std::nullopt; }
  return f;
}
}  // namespace quota_fabric
