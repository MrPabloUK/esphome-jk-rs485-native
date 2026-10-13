#include "jk_rs485_native_master.h"
#include "../jk_rs485_native_bms/jk_rs485_native_bms.h"

#include "esphome/core/log.h"

#include <cinttypes>
#include <cstdio>

namespace esphome {
namespace jk_rs485_native_master {

static const char *const TAG = "jk_rs485_native_master";

void JkRS485NativeMaster::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);
  }
  this->scheduler_.set_timing(this->timing_);
  this->last_rx_byte_ms_ = millis();
  this->last_tx_start_ms_ = millis();
}

void JkRS485NativeMaster::dump_config() {
  ESP_LOGCONFIG(TAG, "JK RS485 native master:");
  ESP_LOGCONFIG(TAG, "  Addresses: %u", (unsigned) this->addresses_.size());
  ESP_LOGCONFIG(TAG, "  response_timeout: %" PRIu32 " ms", this->timing_.response_timeout_ms);
  ESP_LOGCONFIG(TAG, "  inter_byte_timeout: %" PRIu32 " ms", this->timing_.inter_byte_timeout_ms);
  ESP_LOGCONFIG(TAG, "  min_command_gap: %" PRIu32 " ms", this->timing_.min_command_gap_ms);
  ESP_LOGCONFIG(TAG, "  turnaround: %" PRIu32 " ms", this->timing_.turnaround_ms);
  ESP_LOGCONFIG(TAG, "  cell_info: %" PRIu32 " ms", this->timing_.cell_interval_ms);
  ESP_LOGCONFIG(TAG, "  settings: %" PRIu32 " ms", this->timing_.settings_interval_ms);
  LOG_PIN("  Flow control pin: ", this->flow_control_pin_);
}

bool JkRS485NativeMaster::register_device(jk_rs485_native_bms::JkRS485NativeBms *device, uint8_t address) {
  if (address < MIN_ADDRESS || address > MAX_ADDRESS) {
    ESP_LOGE(TAG, "Invalid address 0x%02X", address);
    return false;
  }
  if (this->devices_.count(address) != 0) {
    ESP_LOGE(TAG, "Duplicate address 0x%02X", address);
    return false;
  }
  if (!this->scheduler_.add_address(address)) {
    return false;
  }
  this->devices_[address] = device;
  this->addresses_.push_back(address);
  return true;
}

void JkRS485NativeMaster::set_configured_cell_count(uint8_t address, bool has) {
  this->scheduler_.set_configured_cell_count(address, has);
}

void JkRS485NativeMaster::set_reported_cell_count(uint8_t address, bool has) {
  this->scheduler_.set_reported_cell_count(address, has);
}

bool JkRS485NativeMaster::enqueue_write_uint16(uint8_t address, uint8_t third, uint16_t reg, uint16_t value) {
  Job job;
  job.address = address;
  job.kind = JobKind::WRITE;
  job.tx_kind = TxKind::WRITE;
  job.write_third = third;
  job.write_register = reg;
  job.write_qty = 1;
  job.write_fc = 0x10;
  job.write_frame = build_write_uint16(address, third, reg, value);
  bool ok = this->scheduler_.enqueue_write(job);
  if (!ok) {
    ESP_LOGW(TAG, "Write queue overflow (0x%02X reg 0x%04X)", address, reg);
  }
  if (this->broadcast_changes_) {
    for (uint8_t other : this->addresses_) {
      if (other == address) {
        continue;
      }
      Job copy = job;
      copy.address = other;
      copy.write_frame = build_write_uint16(other, third, reg, value);
      if (!this->scheduler_.enqueue_write(copy)) {
        ESP_LOGW(TAG, "Write queue overflow broadcasting to 0x%02X", other);
      }
    }
  }
  return ok;
}

bool JkRS485NativeMaster::enqueue_write_uint32(uint8_t address, uint8_t third, uint16_t reg, uint32_t value) {
  Job job;
  job.address = address;
  job.kind = JobKind::WRITE;
  job.tx_kind = TxKind::WRITE;
  job.write_third = third;
  job.write_register = reg;
  job.write_qty = 2;
  job.write_fc = 0x10;
  job.write_frame = build_write_uint32(address, third, reg, value);
  bool ok = this->scheduler_.enqueue_write(job);
  if (!ok) {
    ESP_LOGW(TAG, "Write queue overflow (0x%02X reg 0x%04X)", address, reg);
  }
  if (this->broadcast_changes_) {
    for (uint8_t other : this->addresses_) {
      if (other == address) {
        continue;
      }
      Job copy = job;
      copy.address = other;
      copy.write_frame = build_write_uint32(other, third, reg, value);
      this->scheduler_.enqueue_write(copy);
    }
  }
  return ok;
}

bool JkRS485NativeMaster::enqueue_write_int32(uint8_t address, uint8_t third, uint16_t reg, int32_t value) {
  return this->enqueue_write_uint32(address, third, reg, static_cast<uint32_t>(value));
}

void JkRS485NativeMaster::send_frame_(const std::vector<uint8_t> &frame) {
  if (this->debug_dump_frames_) {
    this->log_hex_("TX", frame.data(), frame.size());
  }
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(true);
  }
  this->write_array(frame);
  this->flush();
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(false);
  }
  this->last_tx_start_ms_ = millis();
  this->tx_complete_ms_ = millis();
}

void JkRS485NativeMaster::log_hex_(const char *prefix, const uint8_t *data, size_t len) {
  ESP_LOGI(TAG, "%s (%u): %s", prefix, (unsigned) len, format_hex_pretty(data, len).c_str());
}

void JkRS485NativeMaster::enter_quiet_() {
  this->state_ = TxState::QUIET_GUARD;
  this->has_pending_ = false;
}

void JkRS485NativeMaster::fail_pending_() {
  if (this->has_pending_) {
    this->scheduler_.on_timeout(this->pending_.address, millis());
  }
  this->has_pending_ = false;
  this->state_ = TxState::IDLE;
}

bool JkRS485NativeMaster::write_matches_ack_(const std::vector<uint8_t> &ack) const {
  if (!this->has_pending_ || ack.size() < SHORT_REPLY_SIZE) {
    return false;
  }
  if (ack[0] != this->pending_.address) {
    return false;
  }
  if (ack[1] != this->pending_.write_fc) {
    return false;
  }
  if (ack[2] != this->pending_.write_third) {
    return false;
  }
  if (ack[3] != static_cast<uint8_t>(this->pending_.write_register & 0xFF)) {
    return false;
  }
  if (ack[5] != this->pending_.write_qty) {
    return false;
  }
  return true;
}

void JkRS485NativeMaster::handle_short_(const std::vector<uint8_t> &frame) {
  if (this->state_ == TxState::WAITING_FOR_WRITE_ACK && this->write_matches_ack_(frame)) {
    this->scheduler_.on_write_ok(this->pending_.address, millis());
    this->enter_quiet_();
    return;
  }
  // Trailing ACK after a dump, or unmatched short: consume only.
}

void JkRS485NativeMaster::handle_dump_(const std::vector<uint8_t> &frame) {
  const uint8_t type = dump_frame_type(frame.data());
  const uint8_t wire_addr = dump_address_byte(frame.data());
  uint8_t dispatch_addr = wire_addr;
  bool complete_pending = false;

  if (this->state_ == TxState::WAITING_FOR_DUMP && this->has_pending_) {
    const uint8_t pending_addr = this->pending_.address;
    const uint8_t pending_type = static_cast<uint8_t>(this->pending_.dump_type);
    if (type != pending_type) {
      return;
    }
    if (type == FRAME_SETTINGS) {
      if (wire_addr != pending_addr) {
        ESP_LOGW(TAG, "Settings address byte 0x%02X != pending 0x%02X; dispatching to pending", wire_addr,
                 pending_addr);
      }
      dispatch_addr = pending_addr;
    } else if (wire_addr != pending_addr) {
      ESP_LOGW(TAG, "Crosstalk: dump type 0x%02X addr 0x%02X while pending 0x%02X", type, wire_addr, pending_addr);
      this->scheduler_.on_crosstalk(wire_addr);
      return;
    }
    complete_pending = true;
  } else if (this->devices_.count(dispatch_addr) == 0) {
    return;
  }

  auto it = this->devices_.find(dispatch_addr);
  if (it != this->devices_.end() && it->second != nullptr) {
    it->second->on_frame(type, frame);
  }
  if (complete_pending) {
    this->scheduler_.on_dump_ok(dispatch_addr, static_cast<DumpType>(type), millis());
    this->enter_quiet_();
  }
}

void JkRS485NativeMaster::handle_parse_result_(const ParseResult &result) {
  if (result.kind == ParseKind::DUMP) {
    if (this->debug_dump_frames_) {
      this->log_hex_("RX dump", result.payload.data(), result.payload.size());
    }
    this->handle_dump_(result.payload);
  } else if (result.kind == ParseKind::SHORT_REPLY) {
    if (this->debug_dump_frames_) {
      this->log_hex_("RX ack", result.payload.data(), result.payload.size());
    }
    this->handle_short_(result.payload);
  }
}

void JkRS485NativeMaster::maybe_transmit_() {
  if (this->available() || this->parser_.size() > 0) {
    return;
  }
  if (this->state_ == TxState::WAITING_FOR_DUMP || this->state_ == TxState::WAITING_FOR_WRITE_ACK) {
    return;
  }
  const uint32_t now = millis();
  if (this->state_ == TxState::QUIET_GUARD) {
    if (!this->scheduler_.due_for_tx(now, this->last_tx_start_ms_, this->last_rx_byte_ms_)) {
      return;
    }
    this->state_ = TxState::IDLE;
  }
  auto job = this->scheduler_.next_job(now, this->last_tx_start_ms_, this->last_rx_byte_ms_, true);
  if (!job.has_value()) {
    return;
  }
  this->pending_ = *job;
  this->has_pending_ = true;
  if (job->tx_kind == TxKind::WRITE) {
    this->send_frame_(job->write_frame);
    this->state_ = TxState::WAITING_FOR_WRITE_ACK;
  } else {
    auto frame = build_dump_request(job->address, job->dump_type);
    this->send_frame_(frame);
    this->state_ = TxState::WAITING_FOR_DUMP;
  }
}

void JkRS485NativeMaster::loop() {
  const uint32_t now = millis();
  bool got_byte = false;
  uint8_t byte;
  while (this->available()) {
    if (!this->read_byte(&byte)) {
      break;
    }
    this->parser_.push_byte(byte);
    got_byte = true;
  }
  if (got_byte) {
    this->last_rx_byte_ms_ = now;
    this->last_byte_in_ms_ = now;
  } else if (this->parser_.size() > 0 &&
             Scheduler::elapsed(now, this->last_byte_in_ms_) >= this->timing_.inter_byte_timeout_ms) {
    this->parser_.on_inter_byte_timeout();
  }

  if (this->parser_.size() > 0) {
    ParseResult result = this->parser_.try_parse();
    if (result.kind != ParseKind::NONE && result.kind != ParseKind::NEED_MORE) {
      this->handle_parse_result_(result);
    }
  }

  if (this->has_pending_ &&
      (this->state_ == TxState::WAITING_FOR_DUMP || this->state_ == TxState::WAITING_FOR_WRITE_ACK)) {
    if (Scheduler::elapsed(now, this->tx_complete_ms_) >= this->timing_.response_timeout_ms) {
      ESP_LOGD(TAG, "Timeout waiting for 0x%02X", this->pending_.address);
      this->fail_pending_();
    }
  }

  this->maybe_transmit_();
}

std::string JkRS485NativeMaster::nodes_status_text() const {
  std::string out;
  for (uint8_t addr : this->addresses_) {
    if (!out.empty()) {
      out += ";";
    }
    const char *st = "offline/backoff";
    auto it = this->devices_.find(addr);
    if (it != this->devices_.end() && it->second != nullptr && it->second->is_status_online()) {
      st = "online";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%02X:%s", addr, st);
    out += buf;
  }
  return out;
}

}  // namespace jk_rs485_native_master
}  // namespace esphome
