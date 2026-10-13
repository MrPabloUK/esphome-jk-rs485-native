#include "protocol.h"
#include "frame_parser.h"
#include "scheduler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace esphome::jk_rs485_native_master;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                             \
      g_failed++;                                                                                                      \
    } else {                                                                                                           \
      g_passed++;                                                                                                      \
    }                                                                                                                  \
  } while (0)

static std::vector<uint8_t> make_dump(uint8_t type, uint8_t addr_byte, uint8_t addr300) {
  std::vector<uint8_t> f(DUMP_FRAME_SIZE, 0);
  f[0] = 0x55;
  f[1] = 0xAA;
  f[2] = 0xEB;
  f[3] = 0x90;
  f[4] = type;
  f[DUMP_SETTINGS_ADDRESS_INDEX] = addr_byte;
  f[DUMP_ADDRESS_INDEX] = addr300;
  f[DUMP_CHECKSUM_INDEX] = dump_checksum(f.data(), DUMP_CHECKSUM_COUNT);
  return f;
}

static void test_crc_roundtrip() {
  auto req = build_dump_request(0x01, DumpType::CELL);
  CHECK(req.size() == DUMP_REQUEST_SIZE);
  CHECK(req[0] == 0x01);
  CHECK(req[1] == 0x10);
  CHECK(req[2] == 0x16);
  CHECK(req[3] == 0x20);
  CHECK(check_crc_le(req.data(), 9, req[9], req[10]));

  auto w16 = build_write_uint16(0x02, 0x11, 0x0014, 0x0210);
  CHECK(w16.size() == WRITE_UINT16_SIZE);
  CHECK(check_crc_le(w16.data(), 9, w16[9], w16[10]));

  auto w32 = build_write_uint32(0x02, 0x10, 0x0078, 1);
  CHECK(w32.size() == WRITE_UINT32_SIZE);
  CHECK(check_crc_le(w32.data(), 11, w32[11], w32[12]));
}

static void test_parser_dump_and_ack() {
  FrameParser p;
  auto dump = make_dump(FRAME_CELL, 0x00, 0x01);
  p.push_bytes(dump.data(), dump.size());
  auto r = p.try_parse();
  CHECK(r.kind == ParseKind::DUMP);
  CHECK(r.payload.size() == DUMP_FRAME_SIZE);
  CHECK(dump_frame_type(r.payload.data()) == FRAME_CELL);
  CHECK(dump_address_byte(r.payload.data()) == 0x01);

  auto req = build_dump_request(0x01, DumpType::CELL);
  // 8-byte ACK: addr FC regHi? txubelaxu short is crc over 6 bytes.
  std::vector<uint8_t> ack = {0x01, 0x10, 0x16, 0x20, 0x00, 0x01};
  append_crc_le(ack);
  CHECK(ack.size() == 8);
  FrameParser p2;
  p2.push_bytes(ack.data(), ack.size());
  auto r2 = p2.try_parse();
  CHECK(r2.kind == ParseKind::SHORT_REPLY);
  CHECK(r2.payload.size() == 8);
  CHECK(p2.size() == 0);  // all 8 consumed
}

static void test_ack_then_dump() {
  FrameParser p;
  std::vector<uint8_t> ack = {0x01, 0x10, 0x16, 0x20, 0x00, 0x01};
  append_crc_le(ack);
  auto dump = make_dump(FRAME_CELL, 0, 0x01);
  p.push_bytes(ack.data(), ack.size());
  p.push_bytes(dump.data(), dump.size());
  auto a = p.try_parse();
  CHECK(a.kind == ParseKind::SHORT_REPLY);
  auto d = p.try_parse();
  CHECK(d.kind == ParseKind::DUMP);
}

static void test_dump_then_ack() {
  FrameParser p;
  auto dump = make_dump(FRAME_CELL, 0, 0x01);
  std::vector<uint8_t> ack = {0x01, 0x10, 0x16, 0x20, 0x00, 0x01};
  append_crc_le(ack);
  p.push_bytes(dump.data(), dump.size());
  p.push_bytes(ack.data(), ack.size());
  auto d = p.try_parse();
  CHECK(d.kind == ParseKind::DUMP);
  auto a = p.try_parse();
  CHECK(a.kind == ParseKind::SHORT_REPLY);
}

static void test_duplicate_ack() {
  FrameParser p;
  std::vector<uint8_t> ack = {0x01, 0x10, 0x16, 0x20, 0x00, 0x01};
  append_crc_le(ack);
  p.push_bytes(ack.data(), ack.size());
  p.push_bytes(ack.data(), ack.size());
  CHECK(p.try_parse().kind == ParseKind::SHORT_REPLY);
  CHECK(p.try_parse().kind == ParseKind::SHORT_REPLY);
}

static void test_bad_checksum() {
  FrameParser p;
  auto dump = make_dump(FRAME_CELL, 0, 0x01);
  dump[DUMP_CHECKSUM_INDEX] ^= 0xFF;
  p.push_bytes(dump.data(), dump.size());
  CHECK(p.try_parse().kind == ParseKind::CHECKSUM_FAIL);
}

static void test_partial_timeout() {
  FrameParser p;
  p.push_byte(0x55);
  p.push_byte(0xAA);
  CHECK(p.size() == 2);
  p.on_inter_byte_timeout();
  CHECK(p.size() == 0);
}

static void test_noise_resync() {
  FrameParser p;
  p.push_byte(0x00);
  p.push_byte(0xFF);
  auto dump = make_dump(FRAME_CELL, 0, 0x02);
  p.push_bytes(dump.data(), dump.size());
  auto r1 = p.try_parse();
  CHECK(r1.kind == ParseKind::RESYNC || r1.kind == ParseKind::DUMP);
  if (r1.kind == ParseKind::RESYNC) {
    auto r2 = p.try_parse();
    CHECK(r2.kind == ParseKind::DUMP);
  }
}

static void test_settings_address() {
  auto dump = make_dump(FRAME_SETTINGS, 0x00, 0x99);
  CHECK(dump_address_byte(dump.data()) == 0x00);
  auto cell = make_dump(FRAME_CELL, 0x00, 0x03);
  CHECK(dump_address_byte(cell.data()) == 0x03);
}

static void test_scheduler_floor_and_backoff() {
  Scheduler s;
  TimingConfig t;
  t.cell_interval_ms = 1000;
  t.settings_interval_ms = 30000;
  t.device_info_interval_ms = 3600000;
  t.min_command_gap_ms = 150;
  t.turnaround_ms = 25;
  t.misses_before_backoff = 3;
  t.offline_retry_interval_ms = 5000;
  s.set_timing(t);
  CHECK(s.add_address(0x01));
  CHECK(!s.add_address(0x01));
  CHECK(!s.add_address(0x00));
  s.set_configured_cell_count(0x01, true);

  uint32_t now = 10000;
  auto j = s.next_job(now, 0, 0, true);
  CHECK(j.has_value());
  CHECK(j->dump_type == DumpType::DEVICE_INFO);

  s.on_dump_ok(0x01, DumpType::DEVICE_INFO, now);
  now += 200;
  j = s.next_job(now, now - 200, now - 200, true);
  CHECK(j.has_value());
  CHECK(j->dump_type == DumpType::SETTINGS);
  s.on_dump_ok(0x01, DumpType::SETTINGS, now);

  now += 200;
  j = s.next_job(now, now - 200, now - 200, true);
  CHECK(j.has_value());
  CHECK(j->dump_type == DumpType::CELL);
  s.on_dump_ok(0x01, DumpType::CELL, now);

  now += 200;  // floor 1s not elapsed
  j = s.next_job(now, now - 200, now - 200, true);
  CHECK(!j.has_value());

  now += 900;
  j = s.next_job(now, now - 200, now - 200, true);
  CHECK(j.has_value());
  CHECK(j->dump_type == DumpType::CELL);

  for (int i = 0; i < 3; i++) {
    s.on_timeout(0x01, now);
    now += 10;
  }
  CHECK(s.miss_count(0x01) == 3);
  CHECK(s.is_backed_off(0x01, now));
  j = s.next_job(now, now - 200, now - 200, true);
  CHECK(!j.has_value());
  now += 5000;
  CHECK(!s.is_backed_off(0x01, now));
}

static void test_write_preempt_coalesce_overflow() {
  Scheduler s;
  TimingConfig t;
  t.write_queue_limit = 2;
  t.min_command_gap_ms = 0;
  t.turnaround_ms = 0;
  s.set_timing(t);
  s.add_address(0x01);
  s.add_address(0x02);
  s.set_configured_cell_count(0x01, true);
  s.set_configured_cell_count(0x02, true);
  s.on_dump_ok(0x01, DumpType::DEVICE_INFO, 1);
  s.on_dump_ok(0x01, DumpType::SETTINGS, 1);
  s.on_dump_ok(0x01, DumpType::CELL, 1);
  s.on_dump_ok(0x02, DumpType::DEVICE_INFO, 1);
  s.on_dump_ok(0x02, DumpType::SETTINGS, 1);
  s.on_dump_ok(0x02, DumpType::CELL, 1);

  Job w;
  w.address = 0x01;
  w.tx_kind = TxKind::WRITE;
  w.write_third = 0x10;
  w.write_register = 0x0078;
  w.write_frame = build_write_uint32(0x01, 0x10, 0x0078, 1);
  CHECK(s.enqueue_write(w));
  w.write_frame = build_write_uint32(0x01, 0x10, 0x0078, 0);
  CHECK(s.enqueue_write(w));  // coalesce
  CHECK(s.write_queue_size() == 1);

  Job w2 = w;
  w2.write_register = 0x0070;
  w2.write_frame = build_write_uint32(0x01, 0x10, 0x0070, 1);
  CHECK(s.enqueue_write(w2));
  Job w3 = w;
  w3.write_register = 0x0074;
  CHECK(!s.enqueue_write(w3));  // overflow

  auto j = s.next_job(1000, 0, 0, true);
  CHECK(j.has_value());
  CHECK(j->kind == JobKind::WRITE);

  s.on_write_ok(0x01, 1000);
  j = s.next_job(1200, 1000, 1000, true);
  CHECK(j.has_value());
  CHECK(j->kind == JobKind::WRITE || j->kind == JobKind::VERIFY_SETTINGS);
}

static void test_millis_rollover() {
  uint32_t then = 0xFFFFFFF0u;
  uint32_t now = 0x00000020u;
  CHECK(Scheduler::elapsed(now, then) == 0x30u);
  Scheduler s;
  TimingConfig t;
  t.min_command_gap_ms = 150;
  t.turnaround_ms = 25;
  s.set_timing(t);
  CHECK(s.due_for_tx(now, then, then));
}

static void test_write_ack_sizes() {
  auto w11 = build_write_uint16(0x02, 0x11, 0x0014, 0x0210);
  CHECK(w11.size() == 11);
  std::vector<uint8_t> ack11 = {0x02, 0x10, 0x11, 0x14, 0x00, 0x01};
  append_crc_le(ack11);
  CHECK(ack11.size() == 8);
  CHECK(check_crc_le(ack11.data(), 6, ack11[6], ack11[7]));

  auto w13 = build_write_uint32(0x01, 0x10, 0x0078, 1);
  CHECK(w13.size() == 13);
  std::vector<uint8_t> ack13 = {0x01, 0x10, 0x10, 0x78, 0x00, 0x02};
  append_crc_le(ack13);
  CHECK(ack13.size() == 8);
  CHECK(check_crc_le(ack13.data(), 6, ack13[6], ack13[7]));
}

static void test_malformed_and_wrong_address() {
  FrameParser p;
  p.push_byte(0x01);
  p.push_byte(0x10);
  CHECK(p.try_parse().kind == ParseKind::NEED_MORE);
  auto dump = make_dump(FRAME_CELL, 0, 0x0A);
  CHECK(dump_address_byte(dump.data()) == 0x0A);
  auto settings = make_dump(FRAME_SETTINGS, 0x01, 0x99);
  CHECK(dump_address_byte(settings.data()) == 0x01);
}

static void test_broadcast_queue() {
  Scheduler s;
  TimingConfig t;
  t.write_queue_limit = 16;
  t.min_command_gap_ms = 0;
  t.turnaround_ms = 0;
  s.set_timing(t);
  s.add_address(0x01);
  s.add_address(0x02);
  s.set_configured_cell_count(0x01, true);
  s.set_configured_cell_count(0x02, true);
  Job w;
  w.address = 0x01;
  w.write_third = 0x10;
  w.write_register = 0x0078;
  w.write_frame = build_write_uint32(0x01, 0x10, 0x0078, 1);
  CHECK(s.enqueue_write(w));
  w.address = 0x02;
  w.write_frame = build_write_uint32(0x02, 0x10, 0x0078, 1);
  CHECK(s.enqueue_write(w));
  CHECK(s.write_queue_size() == 2);
}

int main() {
  test_crc_roundtrip();
  test_parser_dump_and_ack();
  test_ack_then_dump();
  test_dump_then_ack();
  test_duplicate_ack();
  test_bad_checksum();
  test_partial_timeout();
  test_noise_resync();
  test_settings_address();
  test_scheduler_floor_and_backoff();
  test_write_preempt_coalesce_overflow();
  test_millis_rollover();
  test_write_ack_sizes();
  test_malformed_and_wrong_address();
  test_broadcast_queue();
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
