# OpenQuatt Modbus and OpenTherm Simulator

Standalone ESPHome firmware for an HCQ Q-edition revision 1.0 board. The board
can simultaneously simulate:

- one OpenTherm boiler/slave on `OTT`;
- two independent Quatt outdoor units on the `M2` RS485 port, normally at
  Modbus addresses 1 and 2.

The combined firmware publishes compatibility contract
`openquatt-modbus-opentherm-v1` and simulator version `v0.1.0`. HIL clients
must verify the contract before changing controller or simulator state.

The project has no runtime or source dependency on OpenQuatt. Its register,
profile and numerical performance data are local snapshots with explicit
provenance. ESPHome is pinned to `2026.8.0`.

Two firmware entrypoints are retained:

| Entrypoint | Function |
|---|---|
| `hcq_v1_boiler_simulator.yaml` | Existing OpenTherm boiler simulator only |
| `hcq_v1_system_simulator.yaml` | OpenTherm boiler plus dual Quatt ODU simulator |

For step-by-step operation and test scenarios, see the Dutch
[tester handleiding](docs/tester-handleiding.md).

## Hardware and wiring

This firmware is only for HCQ revision 1.0. The pin map is based on the
OpenQuatt Q-edition definitions before
[jeroen85/OpenQuatt#303](https://github.com/jeroen85/OpenQuatt/pull/303).

| Function | Connector | HCQ v1.0 GPIO |
|---|---|---:|
| OpenTherm slave input | `OTT` | `GPIO16` |
| OpenTherm slave output | `OTT` | `GPIO15` |
| Yellow status LED | front | `GPIO47` |
| Red status LED | front | `GPIO48` |
| Modbus TX | `M2` | `GPIO40` |
| Modbus RX | `M2` | `GPIO39` |
| Modbus DE/RE | `M2` | `GPIO38` |

`OTT` is the OpenTherm thermostat/slave side and is used here to impersonate a
boiler. `OTB` is the OpenTherm boiler/master side of a controller. `M1` and
`M2` are RS485/Modbus connections, not OpenTherm connections. In this test
setup, the controller-under-test uses its primary ODU RS485 connection and the
simulator uses its onboard `M2` transceiver as server.

```text
Controller/project under test                 HCQ v1.0 simulator

OTB / OpenTherm master ── two wires ───────── OTT / OpenTherm slave

Primary ODU RS485      ── A / B / GND ────── M2 / Modbus RTU server
                                                ├─ address 1: ODU 1
                                                └─ address 2: ODU 2
```

Connect A to A, B to B and GND to GND. OpenTherm polarity does not matter.
Use RS485 termination only at both bus ends. Never connect two OpenTherm
masters, and never put a real ODU with the same Modbus address on this bus.
The simulator drives no relay, pump or other physical actuator.

The M2 bus is fixed at 19200 baud, 8 data bits, even parity and 1 stop bit
(`19200 8E1`). No external MAX485 is needed.

## Build and flash

Run from this repository root:

```sh
python3 -m pip install -r requirements.txt

esphome config hcq_v1_boiler_simulator.yaml
esphome compile hcq_v1_boiler_simulator.yaml

esphome config hcq_v1_system_simulator.yaml
esphome compile hcq_v1_system_simulator.yaml
esphome run hcq_v1_system_simulator.yaml
```

Without station Wi-Fi, the combined firmware starts the access point
`OpenQuatt Simulator` with password `openquatt`. Use the native ESPHome web
page to configure scenarios. Both ODU profiles default to `Disabled`, so the
board does not answer as an ODU until profiles are explicitly selected.

To start a normal PR #534 test:

1. Select `V2 old` for ODU 1 and `V2 new` for ODU 2.
2. Keep their default Modbus addresses 1 and 2.
3. Press `Apply ODU profiles and addresses, then reboot`.
4. Let the controller-under-test rediscover both addresses after the real bus
   interruption caused by the reboot.

Equal addresses are rejected by the apply action. Profile and address changes
only become active after that explicit reboot.

## ODU profiles

| Profile | `2114` | `2122` | `2127` | Customer model | Levels |
|---|---:|---:|---:|---|---:|
| V1 | `0` | `0x0119` | `0x0037` | empty | F0–F10 |
| V1.5 | `0` | `0x011E` | `0x0E37` | empty | F0–F10 |
| V2 old | `2825` | `0x0122` | `0x0E37` | `AMH6` | F0–F10 |
| V2 new | `2825` | `0x0201` | `0x1037` | `AMH6` | F0–F20 |

Factory frequency tables are loaded when a profile is activated. V1 and V1.5
also offer the known `Runtime modified` preset. The controller can write base
tables at `3000..3021`; only V2 new accepts the extension at `3050..3069`.
Writes live in RAM, set the table dirty flag and can be undone with the factory
restore button. Legacy reads of the extension return recognizable poison
values and increment `Legacy extension read violation`.

The model implements actuator writes at `1999`, `2006`, `2010`, `2015` and
`3999`, every runtime register at `2099..2138`, core identity, customer/model
text, synthetic serial metadata and the EEPROM read range used by OpenQuatt.
The generated contract documents every supported address and its confidence:
[docs/quatt-odu-register-contract.md](docs/quatt-odu-register-contract.md).

## Dynamic model

Each ODU has independent mode, level, frequency tables, compressor timers,
pump, flow, temperatures, pressures, fan/EEV state, power, faults and
diagnostics. Heating and cooling select separate tables. Requested levels above
the profile capability are recorded and capped before a frequency is chosen.
Mode changes ramp the compressor to zero before restarting in the other mode.

Pump writes control a settling flow model and the flowswitch. `2138` uses the
OpenQuatt conversion `round(flow_lph / 0.618)`. Water-out temperature follows
the simulated thermal power and mass flow through a first-order response.
`ODU external system pump flow` applies the configured iPWM flow without
requiring ODU pump-relay register `2010`; use it when the controller drives a
separate system pump. Water-in can be set from 0.0 to 60.0 °C in 0.1 °C steps.
Refrigerant temperatures, pressures, fan speed, EEV position and electrical
current are deliberately synthetic but internally related.

The numerical thermal-power/COP snapshot comes from the OpenQuatt source
revision recorded in `data/odu/performance.yaml`; no OpenQuatt header is linked
at runtime. Performance is interpolated by actual frequency. Above 90 Hz the
reported frequency remains correct, while power and COP are capped at the
90 Hz point and `High-frequency performance synthetic` is set.

Defrost can be injected per ODU. The defrost register and status bit are set,
water-out temporarily falls, and `Hold physical level during defrost` controls
whether a new requested F-level is deferred until defrost ends.

Fault words `2119`, `2120` and `2121` accept raw 16-bit values, including
multiple simultaneous and unknown bits. Named bit definitions live in the
register catalog. Pump/no-flow and measured-frequency freeze are separate web
controls.

## Protocol fault injection

The custom server dispatch uses ESPHome's UART, frame timing and Modbus CRC,
but keeps request, pending-response and response buffers fixed-size. It
supports:

- all ODU responses off, or either address independently offline;
- non-blocking response delay;
- every Nth response dropped;
- timeout for one selected start address;
- an injected Modbus exception at one selected start address;
- wrong byte count or an incomplete read response;
- one-shot reboot on a selected request start address.

The diagnostics show request/read/write/drop/exception counts, invalid address
and write counts, capability violations, highest F-level, last request/write
and request age. Reset them before each measurement.

Malformed protocol injection intentionally produces frames a normal Modbus
client should reject. Disable all injections before interpreting normal timing
or functional results.

## Register data and generation

These files are the editable sources of truth:

```text
data/odu/registers.yaml
data/odu/profiles.yaml
data/odu/performance.yaml
data/odu/scenarios.yaml
```

Regenerate C++ tables and documentation with:

```sh
python3 scripts/generate_odu_contract.py
python3 scripts/generate_odu_contract.py --check
```

Confidence values mean:

- `observed`: confirmed in hardware data or a known dump;
- `documented`: from known ODU documentation;
- `controller_contract`: required or decoded by OpenQuatt;
- `synthetic`: chosen for an internally consistent simulation;
- `unknown`: meaning or generation-specific behavior is not established.

Synthetic behavior is never evidence of real ODU behavior.

## Host tests

The pure model and generated contract can be tested without ESPHome or
hardware. On macOS, select the installed Command Line Tools SDK if the default
compiler does not find libc++ headers.

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/boiler_simulator_model_test.cpp -o /tmp/boiler_model_test
/tmp/boiler_model_test
c++ -std=c++17 -Wall -Wextra -Werror tests/opentherm_response_scheduler_test.cpp -o /tmp/ot_scheduler_test
/tmp/ot_scheduler_test
c++ -std=c++17 -Wall -Wextra -Werror tests/quatt_odu_register_contract_test.cpp -o /tmp/odu_contract_test
/tmp/odu_contract_test
c++ -std=c++17 -Wall -Wextra -Werror tests/quatt_odu_fingerprint_test.cpp -o /tmp/odu_fingerprint_test
/tmp/odu_fingerprint_test
c++ -std=c++17 -Wall -Wextra -Werror tests/quatt_odu_simulator_model_test.cpp -o /tmp/odu_model_test
/tmp/odu_model_test
c++ -std=c++17 -Wall -Wextra -Werror tests/quatt_odu_performance_model_test.cpp -o /tmp/odu_performance_test
/tmp/odu_performance_test
```

CI also validates the generated register contract and compiles the combined
ESPHome firmware. Release tags use the simulator version published by
`OpenQuatt Simulator Version`; contract changes require a new contract value,
not only a patch-version bump.

## HIL sequence

Host tests and a successful compile do not prove electrical or timing behavior.
Use this hardware-in-the-loop sequence after flashing:

1. Cold boot with ODU 1 `V2 old`, ODU 2 `V2 new`, normal responses and no
   protocol injection.
2. Reset OT and ODU diagnostics. Confirm both addresses come online with the
   expected fingerprints.
3. Confirm the old profile reads only `3000..3021`; any read at `3050..3069`
   must increment the legacy violation counter. Confirm V2 new reads both.
4. Exercise F0, F10 and F20. ODU 1 must cap F20 to F10; ODU 2 heating F20 must
   report a 110 Hz demand.
5. Exercise heating/cooling, table writes/readback, equal-frequency selection,
   pump/flow, defrost hold and every fault word.
6. Change a profile through Apply/reboot and verify the controller discards its
   old identity/table snapshot and reloads it.
7. Test response loss, reconnect, extension timeout, exception, malformed
   response and reboot between base and extension reads.
8. Run OpenTherm demand in parallel. OT RX overflow, overlap and TX error
   counters must stay zero and queued/completed responses must remain aligned.
9. Repeat cold boot, Wi-Fi reconnect and OTA recovery.
10. Finish with a one-hour soak: active OT polling, both ODU addresses, normal
    OpenQuatt runtime polling, periodic level changes, web UI open and at least
    one reconnect. Record minimum internal heap, largest free internal block,
    fragmentation and task-stack high-watermarks as well as protocol counters.

## Existing OpenTherm behavior

The boiler simulator remains the same component and pin route as the existing
boiler-only firmware. Automatic mode consumes master `Status` and `TSet`,
models ignition/flame/temperature/modulation and stops on stale demand or
fault. Manual telemetry, DHW, fault/status flags, response loss and a 20–700 ms
OpenTherm response delay remain available. The yellow LED follows fresh master
status; the red LED follows simulated fault/diagnostic state.

## Known unknowns and limitations

- Actual V2 hardware behavior for a level above its capability is unknown.
- Real start/stop/ramp behavior and runtime telemetry may differ by generation.
- V2 thermal/electrical behavior at F18–F20 is not validated; power/COP above
  90 Hz are synthetic and capped.
- Reserved registers/bits and most EEPROM metadata have no assigned meaning.
- Real ODU response timing and exception behavior still require captures.
- EEPROM fixtures are synthetic and do not implement a vendor CRC.
- Runtime frequency writes are RAM-only; explicit flash persistence is not
  implemented, avoiding uncontrolled flash wear.
- ODU faults are injected through three raw words; separate named switches,
  timed auto-clear and reboot-latched fault state are not yet implemented.
- Unknown-fingerprint and arbitrary custom-fixture profiles are not yet exposed
  in the web UI; new redacted captures can be added to the catalogs without
  changing the model architecture.
- A one-shot stale pre-profile response is not synthesized. The real reboot and
  configurable loss/malformed modes cover reconnect invalidation tests, but do
  not reproduce every possible stale-frame interleaving.
- A simulator cannot validate combustion, hydraulics, physical heat output or
  undiscovered vendor behavior, and does not replace final testing with a real
  ODU.

## License and provenance

The project is licensed under GPL-3.0. The vendored OpenTherm driver originates
from Ihor Melnyk's MIT-licensed OpenTherm library and includes ESP32-S3
ESP-IDF/RMT adaptations. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
