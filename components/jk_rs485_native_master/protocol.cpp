#include "protocol.h"

namespace esphome {
namespace jk_rs485_native_master {

static constexpr uint16_t CRC16_POLY = 0xA001;

uint16_t crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ CRC16_POLY;
      } else {
        crc = crc >> 1;
      }
    }
  }
  return crc;
}

uint8_t dump_checksum(const uint8_t *data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum = static_cast<uint16_t>(sum + data[i]);
  }
  return static_cast<uint8_t>(sum & 0xFF);
}

// txubelaxu crc16_c returns byte-swapped CRC, then stores:
//   frame[n]   = (swapped >> 8)  = original CRC low
//   frame[n+1] = (swapped & 0xFF) = original CRC high
// which is Modbus little-endian on the wire.
void append_crc_le(std::vector<uint8_t> &frame) {
  uint16_t crc = crc16_modbus(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

bool check_crc_le(const uint8_t *data, size_t payload_len, uint8_t first_crc_byte, uint8_t second_crc_byte) {
  uint16_t crc = crc16_modbus(data, payload_len);
  return first_crc_byte == static_cast<uint8_t>(crc & 0xFF) &&
         second_crc_byte == static_cast<uint8_t>((crc >> 8) & 0xFF);
}

uint8_t dump_register_for_type(DumpType type) {
  switch (type) {
    case DumpType::SETTINGS:
      return 0x1E;
    case DumpType::CELL:
      return 0x20;
    case DumpType::DEVICE_INFO:
      return 0x1C;
  }
  return 0x20;
}

std::vector<uint8_t> build_dump_request(uint8_t address, DumpType type) {
  std::vector<uint8_t> frame;
  frame.reserve(DUMP_REQUEST_SIZE);
  frame.push_back(address);
  frame.push_back(0x10);
  frame.push_back(0x16);
  frame.push_back(dump_register_for_type(type));
  frame.push_back(0x00);
  frame.push_back(0x01);
  frame.push_back(0x02);
  frame.push_back(0x00);
  frame.push_back(0x00);
  append_crc_le(frame);
  return frame;
}

static std::vector<uint8_t> build_write_common(uint8_t address, uint8_t third, uint16_t reg, uint8_t qty_words,
                                               const uint8_t *payload, uint8_t payload_len) {
  std::vector<uint8_t> frame;
  frame.push_back(address);
  frame.push_back(0x10);
  frame.push_back(third);
  frame.push_back(static_cast<uint8_t>(reg & 0xFF));
  frame.push_back(0x00);
  frame.push_back(qty_words);
  frame.push_back(payload_len);
  for (uint8_t i = 0; i < payload_len; i++) {
    frame.push_back(payload[i]);
  }
  append_crc_le(frame);
  return frame;
}

std::vector<uint8_t> build_write_uint16(uint8_t address, uint8_t third, uint16_t reg, uint16_t value) {
  uint8_t payload[2] = {static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
  return build_write_common(address, third, reg, 0x01, payload, 2);
}

std::vector<uint8_t> build_write_uint32(uint8_t address, uint8_t third, uint16_t reg, uint32_t value) {
  uint8_t payload[4] = {static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
                        static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
  return build_write_common(address, third, reg, 0x02, payload, 4);
}

std::vector<uint8_t> build_write_int32(uint8_t address, uint8_t third, uint16_t reg, int32_t value) {
  return build_write_uint32(address, third, reg, static_cast<uint32_t>(value));
}

bool is_dump_preamble(const uint8_t *data, size_t len) {
  if (len < 4) {
    return false;
  }
  return data[0] == 0x55 && data[1] == 0xAA && data[2] == 0xEB && data[3] == 0x90;
}

uint8_t dump_frame_type(const uint8_t *frame) { return frame[DUMP_TYPE_INDEX]; }

uint8_t dump_address_byte(const uint8_t *frame) {
  if (dump_frame_type(frame) == FRAME_SETTINGS) {
    return frame[DUMP_SETTINGS_ADDRESS_INDEX];
  }
  return frame[DUMP_ADDRESS_INDEX];
}

}  // namespace jk_rs485_native_master
}  // namespace esphome
