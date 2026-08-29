#pragma once
// Framed binary TCP protocol. Frame wire format (all multi-byte fields big-endian):
//   [magic u32][version u16][type u8][flags u8][corr_id u64][payload_len u32][payload]
// = 20-byte header. Reads/writes use full loops; malformed/truncated/oversized/
// unsupported/unknown/trailing data is rejected. Never assume one recv == one frame.
#include "quota_fabric/protocol/net.hpp"
#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace quota_fabric {

constexpr std::uint32_t kFrameMagic = 0x51FA0134u;
constexpr std::uint16_t kProtocolVersion = 1;
// Bounded frame size (default 16 MiB). Reject anything larger.
constexpr std::uint32_t kMaxFrameSize = 16u * 1024u * 1024u;

enum class MessageType : std::uint8_t {
  HELLO = 1,                  // agent -> coordinator: register
  WELCOME = 2,                // coordinator -> agent: registered + authority
  CREATE_GROUP = 3,
  CREATE_TENANT = 4,
  SET_TENANT_QUOTA = 5,
  SET_GROUP_QUOTA = 6,
  EVALUATE = 7,
  RESERVE = 8,
  COMMIT_RESERVATION = 9,
  RELEASE = 10,
  START_ALLOCATION = 11,
  RELEASE_ALLOCATION = 12,
  BORROW = 13,
  LEND = 14,
  RECALL_BORROW = 15,
  RECALL_DECISION = 16,
  OBSERVE = 17,
  ENVELOPE = 18,
  SNAPSHOT = 19,
  ADVANCE_EPOCH = 20,
  PING = 21,
  RESULT_OK = 200,
  RESULT_ERR = 255,
};

struct Frame {
  MessageType type = MessageType::PING;
  std::uint8_t flags = 0;
  std::uint64_t correlation = 0;
  std::vector<std::uint8_t> payload;
};

// encode a complete frame (header + payload)
std::vector<std::uint8_t> encode_frame(MessageType type, std::uint64_t corr, std::uint8_t flags,
                                       const std::vector<std::uint8_t>& payload);
// send frame payload bytes over a socket (header included)
bool send_frame(TcpSocket& sock, MessageType type, std::uint64_t corr, std::uint8_t flags,
                const std::vector<std::uint8_t>& payload, std::string* err);
// read one complete frame
std::optional<Frame> recv_frame(TcpSocket& sock, std::uint32_t max_size, std::string* err);

// header-level validation helpers (for adversarially-crafted buffers)
bool validate_frame_header_magic(const std::uint8_t* h);
std::optional<Frame> decode_frame(const std::uint8_t* data, std::size_t n, std::string* err);

}  // namespace quota_fabric
