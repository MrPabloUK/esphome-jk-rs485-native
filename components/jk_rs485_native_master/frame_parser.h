#pragma once

#include "protocol.h"

#include <cstdint>
#include <vector>

namespace esphome {
namespace jk_rs485_native_master {

enum class ParseKind {
  NONE,
  SHORT_REPLY,
  DUMP,
  NEED_MORE,
  RESYNC,
  CHECKSUM_FAIL,
};

struct ParseResult {
  ParseKind kind{ParseKind::NONE};
  std::vector<uint8_t> payload;
};

class FrameParser {
 public:
  void push_byte(uint8_t byte);
  void push_bytes(const uint8_t *data, size_t len);
  ParseResult try_parse();
  void clear();
  size_t size() const { return buffer_.size(); }
  void on_inter_byte_timeout();

 private:
  std::vector<uint8_t> buffer_;
  void erase_prefix_(size_t n);
};

}  // namespace jk_rs485_native_master
}  // namespace esphome
