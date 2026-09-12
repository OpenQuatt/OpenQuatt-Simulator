---
name: hcq-hil
description: Test requested OpenQuatt changes or PRs on the HCQ simulator and test-controller desktop setup. Use when the user says to test changes, a branch, or a PR against the physical HIL lab.
---

# HCQ HIL

When the user says “test deze wijzigingen”, “test deze PR” or equivalent,
start the HIL run. Do not require a testcase identifier or a special prompt.

The fixed lab is:

- HCQ simulator: `http://192.168.2.63/`
- Testcontroller: `http://openquatt-test.local/`; while its renamed test
  firmware is not installed, use `http://192.168.2.86/`.
- The simulator provides OpenTherm boiler/slave on `OTT`, OpenTherm
  thermostat/master on `OTB`, and two ODU simulators on M2 RS485 (`19200 8E1`).
- The testcontroller must be named `openquatt-test`, never `openquatt`, so it
  does not conflict with production at `openquatt.local`.

## Simulator source

The repository root
`/Users/jeroen/Downloads/Codex_projecten/HCQ-OpenTherm-Boiler-Simulator` is a
historic boiler-only checkout. Never use that directory to infer what the
desktop HIL simulator can or cannot simulate.

The canonical source for the flashed desktop setup is the standalone combined
simulator worktree:

```text
/Users/jeroen/Downloads/Codex_projecten/HCQ-OpenTherm-Boiler-Simulator-worktrees/quatt-odu-simulator
```

It must contain all three markers below before making source-based claims about
the simulator:

```text
hcq_v1_system_simulator.yaml
packages/quatt_odu_simulator.yaml
components/quatt_odu_simulator/quatt_odu_simulator.cpp
```

For a PR-specific simulator worktree, use that worktree only when it contains
the same markers. If they are absent, report a checkout mismatch; do not claim
that the simulator lacks an ODU/Modbus model. The combined firmware simulates
the OpenTherm boiler, OpenTherm thermostat and two M2 ODU/Modbus devices.

## Harness

When the active OpenQuatt checkout contains `docs/hil-testing.md` and
`scripts/hil/`, read that guide and use its HIL harness. Pass the lab URLs
explicitly; the harness deliberately has no URL defaults. For a standard
read-only smoke test, use its `run-input-sources.mjs` runner with `--stage
smoke` and the controller/simulator URLs above.

If that harness is not in the active checkout, perform the same reachability
and web-telemetry smoke checks directly. Missing local HIL files are not a
blocker and must never be reported as the reason a test cannot start.

For a controller build that includes OpenQuatt PR #674 or later, use the
dedicated SSE log stream described in [the REST reference](references/esphome-rest.md)
as supplementary evidence for communication, UART, lifecycle or intermittent
failures. Start one bounded capture before the scenario and stop it afterwards.
The stream is diagnostic evidence, not a PASS condition by itself; ordinary
HIL assertions still require observable controller behavior and counters.

For a requested M2 parity- or framing-fault test, read
[the verified REST interface](references/esphome-rest.md) and use
`scripts/run_m2_uart_fault.sh`. This helper makes only the documented simulator
calls, waits for the matching injection counter and turns the injection switch
off on every exit path. A physical fault injection still requires explicit
user authorization; report it as simulator transmission/recovery unless the
controller exposes independent UART-fault evidence.

## Default behavior

- Run read-only preflight automatically.
- Select and run the smallest relevant normal-operation tests. A request to
  test changes/PR authorizes ordinary simulator settings needed for those
  tests, such as demand, room values and normal ODU behavior.
- For a change involving regulation or input handling, use the verified
  simulator input entities in [the REST reference](references/esphome-rest.md)
  to vary room temperature, room setpoint, `TSet`, boiler telemetry or ODU
  water input. Read the allowed range, set one controlled value, verify the
  controller outcome and restore the prior value. Do not conflate simulated
  boiler telemetry with a physical PT1000 supply-sensor test.
- Treat the testcontroller's input-source selection as part of the test state.
  Read the controller source selectors and use its verified `api_input_*` HTTP
  endpoints for controller-side room, outdoor, dew-point, demand and enable
  scenarios. Verify source selection and freshness after every write, then
  restore the previous source and input state. The current API-input surface
  has no water-supply-temperature input; do not substitute another sensor or
  claim a supply-temperature API test.
- Check the actual behavior and relevant counters; do not treat a successful
  request as a pass.
- For diagnosis-relevant changes, include a bounded controller logstream
  capture when the endpoint is present. Do not use more than one stream client
  by default; the controller accepts at most two.
- Restore normal simulator state when finished.
- For a normal “test deze PR”-request, run the read-only smoke test directly.
  Ask one concise question only if the relevant test needs OTA/flash, reboot,
  fault injection, a change outside the requested diff, or an action that
  could affect non-lab equipment.
- Do not invent HTTP endpoints. Use the verified REST reference or the browser
  UI.
- Block only for a real lab issue: unreachable devices, unsafe/unknown
  cabling, an unexpected controller identity, or anomalous behavior.

## Result

Give a compact verdict: what was tested, PASS/FAIL/BLOCKED, and only the
important evidence or blocker. Include detailed values/screenshots when a test
fails or the user asks for them.

For preflight, make ordinary read-only HTTP GET requests to both URLs and
check that their web interfaces respond. Do not require or search for a script.
