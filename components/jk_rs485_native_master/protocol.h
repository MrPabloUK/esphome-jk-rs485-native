#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome {
namespace jk_rs485_native_master {

static constexpr size_t DUMP_FRAME_SIZE = 308;
static constexpr size_t DUMP_REQUEST_SIZE = 11;
static constexpr size_t SHORT_REPLY_SIZE = 8;
static constexpr size_t WRITE_UINT16_SIZE = 11;
static constexpr size_t WRITE_UINT32_SIZE = 13;
static constexpr size_t DUMP_CHECKSUM_COUNT = 299;
static constexpr size_t DUMP_CHECKSUM_INDEX = 299;
static constexpr size_t DUMP_TYPE_INDEX = 4;
static constexpr size_t DUMP_ADDRESS_INDEX = 300;
static constexpr size_t DUMP_SETTINGS_ADDRESS_INDEX = 270;  // 264 + 6
static constexpr uint8_t FRAME_SETTINGS = 0x01;
static constexpr uint8_t FRAME_CELL = 0x02;
static constexpr uint8_t FRAME_DEVICE_INFO = 0x03;
static constexpr uint8_t MIN_ADDRESS = 0x01;
static constexpr uint8_t MAX_ADDRESS = 0x0F;
static constexpr size_t RX_BUFFER_MAX = 4 * DUMP_FRAME_SIZE;

enum class DumpType : uint8_t {
  SETTINGS = FRAME_SETTINGS,
  CELL = FRAME_CELL,
  DEVICE_INFO = FRAME_DEVICE_INFO,
};

// CRC-16/Modbus (poly 0xA001, init 0xFFFF). Returned unswapped.
uint16_t crc16_modbus(const uint8_t *data, size_t len);

// 8-bit sum used on 308-byte dump frames.
uint8_t dump_checksum(const uint8_t *data, size_t len);

// Append CRC as Modbus wire order (low byte first), matching txubelaxu crc16_c placement.
void append_crc_le(std::vector<uint8_t> &frame);

bool check_crc_le(const uint8_t *data, size_t payload_len, uint8_t crc_hi_stored_as_first,
                  uint8_t crc_lo_stored_as_second);

// Dump trigger: addr 10 16 {1E|20|1C} 00 01 02 00 00 CRC
std::vector<uint8_t> build_dump_request(uint8_t address, DumpType type);

// FC 0x10 write, 2-byte payload (11-byte frame) or 4-byte payload (13-byte frame)
std::vector<uint8_t> build_write_uint16(uint8_t address, uint8_t third, uint16_t reg, uint16_t value);
std::vector<uint8_t> build_write_uint32(uint8_t address, uint8_t third, uint16_t reg, uint32_t value);
std::vector<uint8_t> build_write_int32(uint8_t address, uint8_t third, uint16_t reg, int32_t value);

uint8_t dump_register_for_type(DumpType type);

bool is_dump_preamble(const uint8_t *data, size_t len);

uint8_t dump_frame_type(const uint8_t *frame);
uint8_t dump_address_byte(const uint8_t *frame);

}  // namespace jk_rs485_native_master
}  // namespace esphome
