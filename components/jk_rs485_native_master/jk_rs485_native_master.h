#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"

#include "frame_parser.h"
#include "protocol.h"
#include "scheduler.h"

#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace jk_rs485_native_bms {
class JkRS485NativeBms;
}

namespace jk_rs485_native_master {

enum class TxState : uint8_t {
  IDLE,
  WAITING_FOR_DUMP,
  WAITING_FOR_WRITE_ACK,
  QUIET_GUARD,
};

class JkRS485NativeMaster : public uart::UARTDevice, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS - 1.0f; }

  void set_flow_control_pin(GPIOPin *pin) { flow_control_pin_ = pin; }
  void set_response_timeout(uint32_t ms) { timing_.response_timeout_ms = ms; }
  void set_inter_byte_timeout(uint32_t ms) { timing_.inter_byte_timeout_ms = ms; }
  void set_turnaround(uint32_t ms) { timing_.turnaround_ms = ms; }
  void set_min_command_gap(uint32_t ms) { timing_.min_command_gap_ms = ms; }
  void set_offline_retry_interval(uint32_t ms) { timing_.offline_retry_interval_ms = ms; }
  void set_cell_interval(uint32_t ms) { timing_.cell_interval_ms = ms; }
  void set_settings_interval(uint32_t ms) { timing_.settings_interval_ms = ms; }
  void set_device_info_interval(uint32_t ms) { timing_.device_info_interval_ms = ms; }
  void set_debug_dump_frames(bool v) { debug_dump_frames_ = v; }
  void set_broadcast_changes(bool v) { broadcast_changes_ = v; }
  bool get_broadcast_changes() const { return broadcast_changes_; }

  bool register_device(jk_rs485_native_bms::JkRS485NativeBms *device, uint8_t address);
  void set_configured_cell_count(uint8_t address, bool has);
  void set_reported_cell_count(uint8_t address, bool has);

  bool enqueue_write_uint16(uint8_t address, uint8_t third, uint16_t reg, uint16_t value);
  bool enqueue_write_uint32(uint8_t address, uint8_t third, uint16_t reg, uint32_t value);
  bool enqueue_write_int32(uint8_t address, uint8_t third, uint16_t reg, int32_t value);

  std::string nodes_status_text() const;
  const std::vector<uint8_t> &addresses() const { return addresses_; }

 protected:
  GPIOPin *flow_control_pin_{nullptr};
  TimingConfig timing_;
  Scheduler scheduler_;
  FrameParser parser_;
  TxState state_{TxState::IDLE};
  Job pending_;
  bool has_pending_{false};
  uint32_t last_tx_start_ms_{0};
  uint32_t last_rx_byte_ms_{0};
  uint32_t tx_complete_ms_{0};
  uint32_t last_byte_in_ms_{0};
  bool debug_dump_frames_{false};
  bool broadcast_changes_{false};
  std::vector<uint8_t> addresses_;
  std::map<uint8_t, jk_rs485_native_bms::JkRS485NativeBms *> devices_;

  void send_frame_(const std::vector<uint8_t> &frame);
  void handle_parse_result_(const ParseResult &result);
  void handle_dump_(const std::vector<uint8_t> &frame);
  void handle_short_(const std::vector<uint8_t> &frame);
  bool write_matches_ack_(const std::vector<uint8_t> &ack) const;
  void maybe_transmit_();
  void enter_quiet_();
  void fail_pending_();
  void log_hex_(const char *prefix, const uint8_t *data, size_t len);
};

}  // namespace jk_rs485_native_master
}  // namespace esphome
