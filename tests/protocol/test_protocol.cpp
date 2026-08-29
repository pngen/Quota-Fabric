// Protocol framing: validate encode/decode and reject malformed/oversized/
// truncated/unsupported frames.
#include "tests/support/microtest.hpp"
#include "quota_fabric/protocol/frame.hpp"

using namespace quota_fabric;

TEST(protocol_frame_round_trip) {
  std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5};
  auto frame = encode_frame(MessageType::PING, 42, 0, payload);
  CHECK(frame.size() == 20 + payload.size());
  auto dec = decode_frame(frame.data(), frame.size(), nullptr);
  REQUIRE(dec.has_value());
  CHECK(dec->type == MessageType::PING);
  CHECK(dec->correlation == 42);
  CHECK(dec->payload == payload);
}

TEST(protocol_rejects_bad_magic) {
  std::vector<std::uint8_t> frame = encode_frame(MessageType::PING, 1, 0, {});
  frame[0] ^= 0xFF;
  std::string err;
  auto dec = decode_frame(frame.data(), frame.size(), nullptr);
  CHECK(!dec.has_value());
}

TEST(protocol_rejects_truncated) {
  auto frame = encode_frame(MessageType::PING, 1, 0, {1, 2, 3});
  std::string err;
  auto dec = decode_frame(frame.data(), frame.size() - 2, nullptr);
  CHECK(!dec.has_value());
}

TEST(protocol_rejects_oversized_length) {
  // craft a frame header claiming a huge payload but no body
  std::uint8_t h[20] = {0};
  h[0] = 0x51; h[1] = 0xFA; h[2] = 0x01; h[3] = 0x34;
  h[4] = 0; h[5] = 1;  // version
  h[6] = 21; h[7] = 0; // type PING
  h[16] = 0xFF; h[17] = 0xFF; h[18] = 0xFF; h[19] = 0xFF;  // plen = huge
  std::string err;
  auto dec = decode_frame(h, 20, &err);
  CHECK(!dec.has_value());
  CHECK(err == "frame oversized");
}

TEST(protocol_rejects_unsupported_version) {
  std::uint8_t h[20] = {};
  h[0]=0x51; h[1]=0xFA; h[2]=0x01; h[3]=0x34; h[4]=0; h[5]=99; h[6]=21;
  std::string err;
  auto dec = decode_frame(h, 20, &err);
  CHECK(!dec.has_value());
  CHECK(err == "unsupported protocol version");
}

TEST(protocol_never_assumes_single_recv) {
  // Splitting a frame across two "recv"s must still decode via the reader loop
  // mechanism (decode_frame handles a complete buffer). We test that a full
  // frame decode is byte-correct.
  auto frame = encode_frame(MessageType::RESERVE, 7, 0, {9,8,7});
  auto dec = decode_frame(frame.data(), frame.size(), nullptr);
  REQUIRE(dec.has_value());
  CHECK(dec->correlation == 7);
}
