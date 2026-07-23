# HCQ OpenTherm Boiler Simulator

Standalone OpenTherm boiler simulator for a Heatpump Controller Q-edition
revision 1.0 board. The project builds independently and has no runtime or
source dependency on OpenQuatt.

The simulator behaves as an OpenTherm slave. It is intended to test an
OpenTherm master implementation without a physical boiler. It does not produce
heat and cannot validate combustion, hydraulics or vendor-specific boiler
behaviour.

## Hardware target

This firmware is only for HCQ revision 1.0. Its pin map is taken from the
OpenQuatt Q profile before
[jeroen85/OpenQuatt#303](https://github.com/jeroen85/OpenQuatt/pull/303):

| Function | HCQ v1.0 pin |
|---|---:|
| OpenTherm slave input (`OTT`) | `GPIO16` |
| OpenTherm slave output (`OTT`) | `GPIO15` |
| Yellow status LED | `GPIO47` |
| Red status LED | `GPIO48` |

Do not replace these with the current HCQ v1.1 pins. In particular, GPIO47 and
GPIO48 are status LEDs on v1.0; they are not the v1.1 OpenTherm master route.

## Wiring

1. Power both HCQ boards normally and separately.
2. Connect the two wires from the controller-under-test `OTB` terminal to the
   simulator HCQ v1.0 `OTT` terminal.
3. OpenTherm polarity does not matter.
4. Never connect two `OTB` master terminals to each other.

Typical setup:

```text
HCQ v1.1 under test                  HCQ v1.0 simulator
OTB / OpenTherm master  ── 2-wire ── OTT / OpenTherm slave
```

## Build and flash

Install the pinned ESPHome release, then run from this repository root:

```sh
python3 -m pip install -r requirements.txt
esphome config hcq_v1_boiler_simulator.yaml
esphome compile hcq_v1_boiler_simulator.yaml
esphome run hcq_v1_boiler_simulator.yaml
```

Without configured station Wi-Fi the simulator starts the access point
`HCQ Boiler Simulator` with password `openquatt`. The native ESPHome web page
is available through that network.

The deterministic boiler model can also be tested without ESPHome or hardware:

```sh
c++ -std=c++17 -Wall -Wextra -Werror tests/boiler_simulator_model_test.cpp -o /tmp/boiler_simulator_model_test
/tmp/boiler_simulator_model_test
c++ -std=c++17 -Wall -Wextra -Werror tests/opentherm_response_scheduler_test.cpp -o /tmp/opentherm_response_scheduler_test
/tmp/opentherm_response_scheduler_test
```

## Simulator behaviour

Automatic mode consumes the received master `Status` and `TSet` messages:

- valid `CH Enable` and `TSet` start an adjustable ignition delay;
- the simulator then reports `CH active` and `Flame`;
- boiler temperature moves towards `TSet`;
- return temperature follows at an adjustable delta;
- relative modulation follows the remaining temperature error;
- a manual DHW demand has priority when the master permits DHW;
- stale master status and faults stop the simulated flame.

Manual mode permits deliberately inconsistent status combinations, such as
`CH active` without flame, for UI and fault-handling tests. Manual telemetry
allows direct injection of boiler temperature, return temperature and
modulation.

## Fault and link injection

The web controls provide:

- complete response loss and recovery;
- an adjustable response delay from 20 to 700 ms, with 30 ms as the default;
- generic fault and diagnostic indications;
- service request and lockout-reset capability;
- low-water-pressure, flame, air-pressure and overtemperature faults;
- OEM fault and diagnostic codes;
- per-value `DATA_INVALID` injection for temperature, pressure, modulation and
  DHW temperature;
- configurable capacity, minimum modulation and temperature dynamics.

The yellow LED indicates a fresh master Status message. The red LED indicates
a simulated fault or diagnostic condition.

## OpenTherm timing diagnostics

The simulator waits for the configured `OpenTherm response delay` after the
end of a valid master frame before queueing its response. The allowed range
keeps the response inside the OpenTherm slave response window while leaving a
margin below its upper boundary.

The diagnostic entities distinguish the receive, scheduling and transmit
stages:

- `OpenTherm RX capture count` counts frames captured by RMT;
- `OpenTherm invalid request count` counts decoded frames rejected by parity or
  message validation, plus frames rejected by the RMT decoder;
- `OpenTherm RX decode error count` and `OpenTherm RX queue overflow count`
  isolate receive-driver failures;
- the RX glitch-reject, start-bit and timing-error counters split decoder
  failures into actionable classes;
- `OpenTherm response queued count`, `OpenTherm TX completed count` and the
  queue/error counters show whether a generated response reached and completed
  RMT transmission;
- `OpenTherm consecutive duplicate request count` records immediate retries of
  the same request ID. After a master response timeout, an increment proves the
  simulator received the original request; no increment means the original
  request did not reach the simulator;
- the response-turnaround sensors report elapsed time from the captured end of
  the request to queueing the response;
- `OpenTherm last driver error` identifies the latest low-level failure class.

Use `Reset OpenTherm diagnostics` immediately before a measurement interval.
Resetting diagnostics does not interrupt an already scheduled response.

## Suggested HIL sequence

1. Boot with responses enabled, a 30 ms response delay and no heat request.
2. Reset the OpenTherm diagnostics and confirm that the master detects the link
   and reads the static capabilities.
3. Observe a fixed measurement interval. RX overflow, response overlap,
   response queue and TX errors must remain zero. Correlate any RX decoder
   rejects with a master retry or timeout; short idle-bus captures can otherwise
   look like requests. Confirm that queued and completed responses remain
   aligned after allowing for one in-flight frame.
4. Send CH demand and verify ignition, `CH active`, flame and `TSet` tracking.
5. Repeat the measurement after cold boots of simulator and master.
6. If receive errors remain, repeat at 20 and 50 ms and compare the counters.
7. Disable responses and verify link-loss handling on the master.
8. Restore responses and confirm that no stale master command is reused.
9. Inject each invalid telemetry field, fault flag and DHW state.
10. Repeat across master reboot and OTA.

## License and provenance

The project is licensed under GPL-3.0. The vendored OpenTherm communication
driver originates from Ihor Melnyk's MIT-licensed OpenTherm library and includes
ESP32-S3 ESP-IDF/RMT adaptations. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
