# HCQ simulator ESPHome REST interface

The HCQ simulator uses the ESPHome Web Server REST API. Entity names are URL
encoded with `%20` for spaces. State reads are HTTP `GET`; state-changing calls
are HTTP `POST` with an explicit empty body (`-d ''`), which provides the
required `Content-Length: 0` header.

Verified simulator endpoints:

```sh
# Read current values.
curl -fsS 'http://192.168.2.63/switch/M2%20UART%20fault%20injection%20enabled'
curl -fsS 'http://192.168.2.63/sensor/M2%20UART%20parity%20faults%20injected'
curl -fsS 'http://192.168.2.63/sensor/M2%20UART%20framing%20faults%20injected'
curl -fsS 'http://192.168.2.63/sensor/M2%20UART%20fault%20restore%20errors'
curl -fsS 'http://192.168.2.63/binary_sensor/M2%20UART%20fault%20injection%20active'

# Enable only for a one-shot test, then always disable again.
curl -fsS -X POST -d '' \
  'http://192.168.2.63/switch/M2%20UART%20fault%20injection%20enabled/turn_on'
curl -fsS -X POST -d '' \
  'http://192.168.2.63/button/Inject%20M2%20UART%20parity%20error/press'
curl -fsS -X POST -d '' \
  'http://192.168.2.63/switch/M2%20UART%20fault%20injection%20enabled/turn_off'
```

Use `scripts/run_m2_uart_fault.sh` rather than manually combining these calls.
It captures the baseline, enables injection, presses exactly one button, waits
for its matching counter, a clean restore counter and an inactive UART fault,
then always turns the switch off through its exit trap.

The script proves simulator transmission and UART configuration restoration. It
does not prove an internal controller UART filter without a controller-side
counter or log signal.

## Controlled HIL inputs

Use the simulator as a controlled source of input values, not just as a fault
injector. The following `number` entities and ranges are verified on the
desktop simulator:

| Entity | Range | Controller-facing use |
|---|---:|---|
| `Thermostat room temperature` | -20..50 °C, 0.5 °C | OpenTherm room temperature |
| `Thermostat room setpoint` | 5..35 °C, 0.5 °C | OpenTherm room setpoint |
| `Thermostat TSet` | 5..90 °C, 0.5 °C | OpenTherm control setpoint |
| `Manual boiler temperature` | 0..120 °C, 0.5 °C | Boiler temperature, only with `Manual telemetry` enabled |
| `ODU 1/2 water-in temperature` | 0..60 °C, 0.1 °C | ODU water-circuit input |

Read an input and its supported range before changing it. Set a value with a
POST, then GET it back and verify the controller's resulting value or behavior
before proceeding. Example:

```sh
curl -fsS 'http://192.168.2.63/number/Thermostat%20room%20temperature?detail=all'
curl -fsS -X POST -d '' \
  'http://192.168.2.63/number/Thermostat%20room%20temperature/set?value=21.0'
curl -fsS 'http://192.168.2.63/number/Thermostat%20room%20temperature'
```

`Manual boiler temperature` is not a substitute for the controller's physical
PT1000 supply sensor. Do not claim a physical supply-sensor test from that
entity. Restore changed input values at the end of a test unless the requested
test intentionally leaves a reproducible scenario configured.

## Testcontroller input path

The controller is not only an observation target. It accepts external source
values through its local ESPHome webserver API. Use the documented underscore
entity IDs below, not the human-facing names shown in the web app. The
following controller REST reads are verified on the lab controller:

```sh
curl -fsS 'http://openquatt-test.local/number/api_input_room_temperature?detail=all'
curl -fsS 'http://openquatt-test.local/number/api_input_room_setpoint?detail=all'
curl -fsS 'http://openquatt-test.local/select/Room%20Temperature%20Source?detail=all'
curl -fsS 'http://openquatt-test.local/select/Room%20Setpoint%20Source?detail=all'
```

| Input | POST endpoint | Range / validity |
|---|---|---|
| Cooling dew point | `/number/api_input_cooling_dew_point/set?value=X` | -20..35 °C, 15 min |
| Outside temperature | `/number/api_input_outside_temperature/set?value=X` | -40..60 °C, 30 min |
| Room temperature | `/number/api_input_room_temperature/set?value=X` | 0..50 °C, 10 min |
| Room setpoint | `/number/api_input_room_setpoint/set?value=X` | 5..35 °C, until reboot/new value |
| External heat demand | `/number/api_input_external_heat_demand/set?value=X` | 0..15000 W, 15 min |
| Heating enable | `/switch/api_input_heating_enable/turn_on` or `/turn_off` | until reboot/new value |
| Cooling enable | `/switch/api_input_cooling_enable/turn_on` or `/turn_off` | until reboot/new value |

Use `POST -H 'Content-Length: 0'` and follow every write with a GET. Select
`API input` on the matching controller source before interpreting the control
result, then restore the prior source and input state after the test. Example:

```sh
curl -fsS -X POST -H 'Content-Length: 0' \
  'http://openquatt-test.local/number/api_input_room_temperature/set?value=20.5'
curl -fsS -X POST -H 'Content-Length: 0' \
  'http://openquatt-test.local/select/Room%20Temperature%20Source/set?option=API%20input'
```

The current controller API-input surface does not include water supply
temperature: `Water Supply Source` offers `Local`, `CIC` and `HA input`, but
not `API input`. Do not claim a controller API-supply-temperature test until
that product input exists. Use the thermostat simulator for physical OpenTherm
room/setpoint tests; use the controller API inputs to test source selection,
freshness, fallback and strategy behavior.

## Controller log stream

OpenQuatt PR #674 adds a dedicated Server-Sent Events endpoint for new
controller log lines:

```sh
curl -fsS -N --max-time 120 \
  'http://openquatt-test.local/openquatt/logs/stream' > controller-logstream.txt || [ $? -eq 28 ]
```

Use it for a bounded capture around a HIL scenario that needs diagnostic
evidence, for example M2 UART recovery, Modbus timeouts or an intermittent
lifecycle issue. Start the capture before the stimulus and stop it afterwards;
keep the artifact with the test result. A failed endpoint (for example HTTP
404 on older firmware) means the stream is unavailable, not that the HIL run
must stop.

Exit status `28` is expected when the 120-second capture reaches its intended
time limit; other `curl` errors are failures to record the requested evidence.

The controller supports at most two stream clients. Use one client by default,
never leave an unbounded capture running, and do not treat log text alone as a
PASS verdict. `/openquatt/logs/recent` remains the bounded history/backfill
endpoint; the stream accepts `?since=`, `?last_seq=` or `Last-Event-ID` for
reconnect when a specific investigation needs continuity.
