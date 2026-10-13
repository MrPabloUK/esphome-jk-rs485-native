#pragma once

#include "protocol.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace esphome {
namespace jk_rs485_native_master {

enum class JobKind : uint8_t {
  WRITE,
  VERIFY_SETTINGS,
  DEVICE_INFO,
  SETTINGS,
  CELL,
};

enum class TxKind : uint8_t {
  DUMP,
  WRITE,
};

struct Job {
  uint8_t address{0};
  JobKind kind{JobKind::CELL};
  DumpType dump_type{DumpType::CELL};
  TxKind tx_kind{TxKind::DUMP};
  std::vector<uint8_t> write_frame;
  uint8_t write_fc{0x10};
  uint8_t write_third{0};
  uint16_t write_register{0};
  uint8_t write_qty{0};
};

struct TimingConfig {
  uint32_t min_command_gap_ms{150};
  uint32_t turnaround_ms{25};
  uint32_t response_timeout_ms{200};
  uint32_t inter_byte_timeout_ms{30};
  uint32_t cell_interval_ms{1000};
  uint32_t settings_interval_ms{30000};
  uint32_t device_info_interval_ms{3600000};
  uint32_t offline_retry_interval_ms{5000};
  uint8_t misses_before_backoff{3};
  size_t write_queue_limit{16};
};

struct AddressState {
  uint8_t address{0};
  uint32_t last_cell_ok_ms{0};
  uint32_t last_settings_ok_ms{0};
  uint32_t last_device_info_ok_ms{0};
  uint32_t last_attempt_ms{0};
  uint8_t miss_count{0};
  bool has_reported_cell_count{false};
  bool configured_cell_count{false};
  bool verify_settings_pending{false};
};

class Scheduler {
 public:
  void set_timing(const TimingConfig &t) { timing_ = t; }
  const TimingConfig &timing() const { return timing_; }

  bool add_address(uint8_t address);
  void set_configured_cell_count(uint8_t address, bool has);
  void set_reported_cell_count(uint8_t address, bool has);

  // Returns false on overflow. Coalesces same address+third+register.
  bool enqueue_write(const Job &job);
  size_t write_queue_size() const { return writes_.size(); }

  std::optional<Job> next_job(uint32_t now, uint32_t last_tx_start_ms, uint32_t last_rx_byte_ms, bool tx_allowed);

  void on_dump_ok(uint8_t address, DumpType type, uint32_t now);
  void on_write_ok(uint8_t address, uint32_t now);
  void on_timeout(uint8_t address, uint32_t now);
  void on_crosstalk(uint8_t address);

  bool is_backed_off(uint8_t address, uint32_t now) const;
  uint8_t miss_count(uint8_t address) const;
  const std::vector<AddressState> &addresses() const { return addresses_; }

  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

  bool due_for_tx(uint32_t now, uint32_t last_tx_start_ms, uint32_t last_rx_byte_ms) const;

 private:
  TimingConfig timing_;
  std::vector<AddressState> addresses_;
  std::deque<Job> writes_;
  size_t rr_index_{0};

  AddressState *find_(uint8_t address);
  const AddressState *find_(uint8_t address) const;
  bool settings_needed_before_cell_(const AddressState &st) const;
};

}  // namespace jk_rs485_native_master
}  // namespace esphome
