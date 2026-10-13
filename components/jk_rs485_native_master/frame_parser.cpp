#include "frame_parser.h"

#include <algorithm>
#include <cstring>

namespace esphome {
namespace jk_rs485_native_master {

void FrameParser::push_byte(uint8_t byte) {
  if (buffer_.size() >= RX_BUFFER_MAX) {
    buffer_.clear();
  }
  buffer_.push_back(byte);
}

void FrameParser::push_bytes(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    push_byte(data[i]);
  }
}

void FrameParser::clear() { buffer_.clear(); }

void FrameParser::erase_prefix_(size_t n) {
  if (n == 0) {
    return;
  }
  if (n >= buffer_.size()) {
    buffer_.clear();
    return;
  }
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(n));
}

void FrameParser::on_inter_byte_timeout() {
  if (!buffer_.empty()) {
    buffer_.clear();
  }
}

ParseResult FrameParser::try_parse() {
  ParseResult result;
  if (buffer_.empty()) {
    result.kind = ParseKind::NONE;
    return result;
  }

  static const uint8_t PREAMBLE[4] = {0x55, 0xAA, 0xEB, 0x90};
  auto preamble_it = std::search(buffer_.begin(), buffer_.end(), PREAMBLE, PREAMBLE + 4);
  const bool preamble_found = preamble_it != buffer_.end();
  const size_t preamble_index =
      preamble_found ? static_cast<size_t>(std::distance(buffer_.begin(), preamble_it)) : buffer_.size();

  // Prefer an 8-byte FC10 ACK over an 11-byte dump request: an ACK followed by a dump
  // (55 AA ...) shares the first 6 payload bytes of a request, so checking the request
  // first would swallow the ACK CRC plus the dump preamble.
  if (buffer_.size() >= SHORT_REPLY_SIZE) {
    const uint8_t *raw = buffer_.data();
    const bool ack_before_dump = !preamble_found || preamble_index >= SHORT_REPLY_SIZE;
    if (ack_before_dump && check_crc_le(raw, 6, raw[6], raw[7])) {
      result.kind = ParseKind::SHORT_REPLY;
      result.payload.assign(raw, raw + SHORT_REPLY_SIZE);
      erase_prefix_(SHORT_REPLY_SIZE);  // consume all 8 bytes (fixes upstream short_size-1 bug)
      return result;
    }
  }

  if (buffer_.size() >= DUMP_REQUEST_SIZE && (!preamble_found || preamble_index >= DUMP_REQUEST_SIZE)) {
    const uint8_t *raw = buffer_.data();
    if (check_crc_le(raw, 9, raw[9], raw[10]) && raw[1] == 0x10 && raw[2] == 0x16) {
      // Master request seen on the wire (second master). Consume it; caller may log.
      result.kind = ParseKind::RESYNC;
      erase_prefix_(DUMP_REQUEST_SIZE);
      return result;
    }
  }

  if (buffer_.size() < DUMP_FRAME_SIZE) {
    result.kind = ParseKind::NEED_MORE;
    return result;
  }

  if (preamble_found) {
    if (preamble_index > 0) {
      erase_prefix_(preamble_index);
      result.kind = ParseKind::RESYNC;
      return result;
    }
  } else {
    erase_prefix_(buffer_.size() > 3 ? buffer_.size() - 3 : buffer_.size());
    result.kind = ParseKind::RESYNC;
    return result;
  }

  const uint8_t *raw = buffer_.data();
  uint8_t computed = dump_checksum(raw, DUMP_CHECKSUM_COUNT);
  if (computed != raw[DUMP_CHECKSUM_INDEX]) {
    auto it_next = std::search(buffer_.begin() + 1, buffer_.end(), PREAMBLE, PREAMBLE + 4);
    size_t index_next = static_cast<size_t>(std::distance(buffer_.begin(), it_next));
    erase_prefix_(index_next);
    result.kind = ParseKind::CHECKSUM_FAIL;
    return result;
  }

  result.kind = ParseKind::DUMP;
  result.payload.assign(raw, raw + DUMP_FRAME_SIZE);
  erase_prefix_(DUMP_FRAME_SIZE);
  return result;
}

}  // namespace jk_rs485_native_master
}  // namespace esphome
