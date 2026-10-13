#include "scheduler.h"

namespace esphome {
namespace jk_rs485_native_master {

bool Scheduler::add_address(uint8_t address) {
  if (address < MIN_ADDRESS || address > MAX_ADDRESS) {
    return false;
  }
  if (find_(address) != nullptr) {
    return false;
  }
  AddressState st;
  st.address = address;
  addresses_.push_back(st);
  return true;
}

void Scheduler::set_configured_cell_count(uint8_t address, bool has) {
  if (auto *st = find_(address)) {
    st->configured_cell_count = has;
  }
}

void Scheduler::set_reported_cell_count(uint8_t address, bool has) {
  if (auto *st = find_(address)) {
    st->has_reported_cell_count = has;
  }
}

AddressState *Scheduler::find_(uint8_t address) {
  for (auto &st : addresses_) {
    if (st.address == address) {
      return &st;
    }
  }
  return nullptr;
}

const AddressState *Scheduler::find_(uint8_t address) const {
  for (const auto &st : addresses_) {
    if (st.address == address) {
      return &st;
    }
  }
  return nullptr;
}

uint8_t Scheduler::miss_count(uint8_t address) const {
  const auto *st = find_(address);
  return st ? st->miss_count : 0;
}

bool Scheduler::is_backed_off(uint8_t address, uint32_t now) const {
  const auto *st = find_(address);
  if (st == nullptr) {
    return false;
  }
  if (st->miss_count < timing_.misses_before_backoff) {
    return false;
  }
  if (st->last_attempt_ms == 0) {
    return false;
  }
  return elapsed(now, st->last_attempt_ms) < timing_.offline_retry_interval_ms;
}

bool Scheduler::settings_needed_before_cell_(const AddressState &st) const {
  return !st.configured_cell_count && !st.has_reported_cell_count;
}

bool Scheduler::enqueue_write(const Job &job) {
  for (auto &existing : writes_) {
    if (existing.address == job.address && existing.write_third == job.write_third &&
        existing.write_register == job.write_register) {
      existing = job;
      return true;
    }
  }
  if (writes_.size() >= timing_.write_queue_limit) {
    return false;
  }
  writes_.push_back(job);
  return true;
}

bool Scheduler::due_for_tx(uint32_t now, uint32_t last_tx_start_ms, uint32_t last_rx_byte_ms) const {
  if (elapsed(now, last_tx_start_ms) < timing_.min_command_gap_ms) {
    return false;
  }
  if (elapsed(now, last_rx_byte_ms) < timing_.turnaround_ms) {
    return false;
  }
  return true;
}

std::optional<Job> Scheduler::next_job(uint32_t now, uint32_t last_tx_start_ms, uint32_t last_rx_byte_ms,
                                       bool tx_allowed) {
  if (!tx_allowed || addresses_.empty()) {
    return std::nullopt;
  }
  if (!due_for_tx(now, last_tx_start_ms, last_rx_byte_ms)) {
    return std::nullopt;
  }

  if (!writes_.empty()) {
    Job j = writes_.front();
    writes_.pop_front();
    j.kind = JobKind::WRITE;
    j.tx_kind = TxKind::WRITE;
    if (auto *st = find_(j.address)) {
      st->last_attempt_ms = now;
    }
    return j;
  }

  auto make_dump = [](uint8_t addr, DumpType type, JobKind kind) {
    Job j;
    j.address = addr;
    j.kind = kind;
    j.dump_type = type;
    j.tx_kind = TxKind::DUMP;
    return j;
  };

  const size_t n = addresses_.size();
  for (size_t i = 0; i < n; i++) {
    size_t idx = (rr_index_ + i) % n;
    AddressState &st = addresses_[idx];
    if (is_backed_off(st.address, now)) {
      continue;
    }

    auto take_dump = [&](DumpType type, JobKind kind) {
      st.last_attempt_ms = now;
      rr_index_ = (idx + 1) % n;
      return make_dump(st.address, type, kind);
    };
    if (st.verify_settings_pending) {
      return take_dump(DumpType::SETTINGS, JobKind::VERIFY_SETTINGS);
    }
    if (st.last_device_info_ok_ms == 0 ||
        elapsed(now, st.last_device_info_ok_ms) >= timing_.device_info_interval_ms) {
      return take_dump(DumpType::DEVICE_INFO, JobKind::DEVICE_INFO);
    }
    if (st.last_settings_ok_ms == 0 || elapsed(now, st.last_settings_ok_ms) >= timing_.settings_interval_ms ||
        settings_needed_before_cell_(st)) {
      return take_dump(DumpType::SETTINGS, JobKind::SETTINGS);
    }
    if (timing_.cell_interval_ms == 0 || st.last_cell_ok_ms == 0 ||
        elapsed(now, st.last_cell_ok_ms) >= timing_.cell_interval_ms) {
      return take_dump(DumpType::CELL, JobKind::CELL);
    }
  }
  return std::nullopt;
}

void Scheduler::on_dump_ok(uint8_t address, DumpType type, uint32_t now) {
  auto *st = find_(address);
  if (st == nullptr) {
    return;
  }
  st->miss_count = 0;
  st->last_attempt_ms = now;
  if (type == DumpType::CELL) {
    st->last_cell_ok_ms = now;
  } else if (type == DumpType::SETTINGS) {
    st->last_settings_ok_ms = now;
    st->verify_settings_pending = false;
  } else if (type == DumpType::DEVICE_INFO) {
    st->last_device_info_ok_ms = now;
  }
}

void Scheduler::on_write_ok(uint8_t address, uint32_t now) {
  auto *st = find_(address);
  if (st == nullptr) {
    return;
  }
  st->miss_count = 0;
  st->verify_settings_pending = true;
  (void) now;
}

void Scheduler::on_timeout(uint8_t address, uint32_t now) {
  auto *st = find_(address);
  if (st == nullptr) {
    return;
  }
  if (st->miss_count < 255) {
    st->miss_count++;
  }
  st->last_attempt_ms = now;
}

void Scheduler::on_crosstalk(uint8_t address) { (void) address; }

}  // namespace jk_rs485_native_master
}  // namespace esphome
