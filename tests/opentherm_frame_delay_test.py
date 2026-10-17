#!/usr/bin/env python3
"""Compile the actual OpenTherm process/reset bodies with deterministic clocks.

Only RX/TX hardware, parity validation and callbacks are stubbed. This exercises
DELAY entry, release and reset, not RMT capture or real scheduler timing. The
optional component directory permits running this regression against old source.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def harness(component):
    cpp = (component / "OpenTherm.cpp").read_text()
    header = (component / "OpenTherm.h").read_text()
    declaration = re.search(r"uint(?:32|64)_t (delay_until_(?:ts|us)_) = 0;", header)
    if declaration is None:
        raise ValueError("OpenTherm DELAY member declaration not found")
    constant = re.search(r"static constexpr uint32_t OT_FRAME_DELAY_US = \d+;", cpp)
    if constant is None:
        raise ValueError("OpenTherm inter-frame gap constant not found")
    source = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define HCQ_OT_RMT_SUPPORTED 1
#define portENTER_CRITICAL(...) ((void) 0)
#define portEXIT_CRITICAL(...) ((void) 0)
PRODUCTION_ENUMS
PRODUCTION_CONSTANT
uint64_t clock_us = 0;
uint32_t cycle_offset = 0;
uint32_t now_cycles() { return static_cast<uint32_t>(clock_us * 240ULL + cycle_offset); }
int64_t esp_timer_get_time() { return static_cast<int64_t>(clock_us); }
class OpenTherm {
 public:
  bool process();
  void reset_receive_state_();
  struct RxFrame {};
  bool pop_rx_frame_(RxFrame &) { return false; }
  void process_rmt_frame_(const RxFrame &) {}
  void setIdleState() {}
  bool isValidParity(unsigned long) { return true; }
  bool isValidRequest(unsigned long) { return true; }
  bool isValidResponse(unsigned long) { return true; }
  OpenThermStatus status = OpenThermStatus::READY;
  OpenThermResponseStatus responseStatus = OpenThermResponseStatus::NONE;
  unsigned long response = 0, responseTimestamp = 0;
  uint32_t cycles_per_us_ = 240, last_rx_frame_end_cycles_ = 0;
  bool isSlave = false, edge_overflow_ = false;
  bool tx_in_progress_ = false, tx_complete_ = false;
  uint8_t rx_frame_head_ = 0, rx_frame_tail_ = 0;
  static constexpr uint8_t PROCESS_MAX_RX_FRAMES = 2;
  static constexpr uint32_t PROCESS_BUDGET_US = 250;
  PRODUCTION_DEADLINE
  void (*processResponseCallback)(unsigned long, OpenThermResponseStatus, void *) = nullptr;
  void *pCallbackUser = nullptr;
};
PRODUCTION_METHODS
struct Callback {
  unsigned calls = 0;
  uint64_t elapsed_us = 0;
  OpenTherm *start_tx = nullptr;
};
void on_response(unsigned long, OpenThermResponseStatus status, void *context) {
  assert(status == OpenThermResponseStatus::SUCCESS);
  auto &callback = *static_cast<Callback *>(context);
  ++callback.calls;
  clock_us += callback.elapsed_us;
  if (callback.start_tx != nullptr) {
    callback.start_tx->status = OpenThermStatus::REQUEST_SENDING;
    callback.start_tx->tx_in_progress_ = true;
    callback.start_tx->tx_complete_ = true;
    callback.start_tx->responseTimestamp = now_cycles();
  }
}
void enter_delay(OpenTherm &ot, Callback &callback, uint32_t cycles, bool slave = false,
                 uint32_t capture_age_cycles = 0) {
  clock_us = 1000000;
  cycle_offset = cycles - static_cast<uint32_t>(clock_us * 240ULL);
  ot.isSlave = slave;
  ot.status = OpenThermStatus::RESPONSE_READY;
  ot.responseTimestamp = static_cast<uint32_t>(now_cycles() - capture_age_cycles);
  ot.processResponseCallback = on_response;
  ot.pCallbackUser = &callback;
  assert(ot.process());
  assert(callback.calls == 1);
}
void expect_delay(OpenTherm &ot, Callback &callback) {
  assert(!ot.process());
  assert(ot.status == OpenThermStatus::DELAY);
  assert(callback.calls == 1);
}
void expect_ready(OpenTherm &ot, Callback &callback) {
  assert(!ot.process());
  assert(ot.status == OpenThermStatus::READY);
  assert(callback.calls == 1);
  assert(ot.DEADLINE_MEMBER == 0);
  assert(!ot.process());
  assert(callback.calls == 1);
}
void check(const char *scenario, bool slave) {
  OpenTherm ot;
  Callback callback;
  if (std::strcmp(scenario, "normal") == 0) {
    enter_delay(ot, callback, 100000000U, slave);
    clock_us += 99999;
    expect_delay(ot, callback);
    ++clock_us;
    expect_ready(ot, callback);
  } else if (std::strcmp(scenario, "late-wrap") == 0) {
    enter_delay(ot, callback, UINT32_MAX - 24000000U - 255U, slave);
    clock_us += 96000;
    expect_delay(ot, callback);
    clock_us += 16000;
    expect_ready(ot, callback);
  } else if (std::strcmp(scenario, "premature-wrap") == 0) {
    enter_delay(ot, callback, UINT32_MAX - 12000000U + 1U, slave);
    clock_us += 16000;
    expect_delay(ot, callback);
    clock_us += 84000;
    expect_ready(ot, callback);
  } else if (std::strcmp(scenario, "zero-deadline") == 0) {
    enter_delay(ot, callback, UINT32_MAX - 24000000U + 1U, slave, 1000000);
    clock_us += 99999;
    expect_delay(ot, callback);
    ++clock_us;
    expect_ready(ot, callback);
  } else if (std::strcmp(scenario, "long-pause") == 0) {
    enter_delay(ot, callback, UINT32_MAX - 24000000U - 255U, slave);
    clock_us += 20000000;  // Beyond both half-wrap and a complete CPU cycle wrap.
    expect_ready(ot, callback);
  } else if (std::strcmp(scenario, "callback-time") == 0) {
    callback.elapsed_us = 75000;
    enter_delay(ot, callback, 100000000U, slave);
    clock_us += 24999;
    expect_delay(ot, callback);
    ++clock_us;
    expect_ready(ot, callback);
  } else if (std::strcmp(scenario, "callback-tx") == 0) {
    callback.start_tx = &ot;
    enter_delay(ot, callback, 100000000U, slave);
    assert(ot.status == OpenThermStatus::REQUEST_SENDING);
    assert(!ot.process());
    assert(ot.status == OpenThermStatus::READY);
    assert(callback.calls == 1);
  } else if (std::strcmp(scenario, "reset") == 0) {
    enter_delay(ot, callback, 100000000U, slave);
    assert(ot.DEADLINE_MEMBER != 0);
    ot.reset_receive_state_();
    assert(ot.DEADLINE_MEMBER == 0);
    ot.status = OpenThermStatus::READY;  // begin() restores READY after RX/TX setup.
    callback = {};
    enter_delay(ot, callback, 200000000U, slave);
    clock_us += 99999;
    expect_delay(ot, callback);
    ++clock_us;
    expect_ready(ot, callback);
  } else {
    assert(false && "Unknown test scenario");
  }
}
int main(int argc, char **argv) {
  assert(argc == 2);
  for (bool slave : {false, true}) check(argv[1], slave);
  std::puts("OpenTherm production DELAY scenario passed (master + slave)");
}
'''
    source = source.replace("PRODUCTION_ENUMS", "\n".join(
        block(header, "enum " + name) + ";"
        for name in ("OpenThermStatus", "OpenThermResponseStatus")))
    source = source.replace("PRODUCTION_CONSTANT", constant.group())
    source = source.replace("PRODUCTION_DEADLINE", declaration.group())
    source = source.replace("DEADLINE_MEMBER", declaration.group(1))
    source = source.replace("PRODUCTION_METHODS", "\n".join(
        block(cpp, signature) for signature in
        ("bool OpenTherm::process()", "void OpenTherm::reset_receive_state_()")))
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component-dir", type=Path,
                        default=ROOT / "components/hcq_ot_boiler_simulator")
    parser.add_argument("--emit-source", type=Path,
                        help="Write the fixture without compiling or running it")
    args = parser.parse_args()
    source = harness(args.component_dir)
    if args.emit_source is not None:
        args.emit_source.write_text(source)
        return
    with tempfile.TemporaryDirectory(prefix="opentherm-delay-") as directory:
        cpp = Path(directory) / "fixture.cpp"
        binary = Path(directory) / "fixture"
        cpp.write_text(source)
        subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17",
                        "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        failed = []
        for scenario in ("normal", "late-wrap", "premature-wrap", "zero-deadline",
                         "long-pause", "callback-time", "callback-tx", "reset"):
            result = subprocess.run([str(binary), scenario], check=False)
            if result.returncode:
                failed.append(scenario)
        if failed:
            raise SystemExit("Failed production DELAY scenarios: " + ", ".join(failed))


if __name__ == "__main__":
    main()
