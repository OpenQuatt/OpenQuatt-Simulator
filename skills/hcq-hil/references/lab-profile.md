# HCQ desktop lab profile

Use this profile when testing OpenQuatt changes on Jeroen's fixed desktop lab.

- HCQ simulator: `http://192.168.2.63/`
- Testcontroller: `http://openquatt-test.local/`; until its dedicated HIL
  firmware is flashed, use the DHCP fallback `http://192.168.2.86/`.
- The simulator provides the controller's OpenTherm boiler/slave on `OTT`, the
  controller's OpenTherm thermostat/master on `OTB`, and two ODU simulators on
  `M2` RS485 (`19200 8E1`).

The controller name must be `openquatt-test`, never `openquatt`, to avoid
conflicting with the production controller at `openquatt.local`. A test may
run normally using the DHCP fallback until that rename firmware is installed.
