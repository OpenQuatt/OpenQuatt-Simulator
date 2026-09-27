# HCQ HIL-interfacecatalogus

Gebruik deze catalogus als grens voor geautomatiseerde communicatie. Voeg een
nieuwe actie pas toe nadat deze op de fysieke opstelling is geverifieerd,
inclusief doel, parameters, voorbeeld, verwacht antwoord en risico.

## Geverifieerde read-only requests

De ESPHome-webservers accepteren geen `HEAD`; gebruik een gewone `GET`.

```sh
curl -sS -o /dev/null -w '%{http_code}\n' --max-time 5 http://192.168.2.63/
curl -sS -o /dev/null -w '%{http_code}\n' --max-time 5 http://openquatt-test.local/
```

Beide moeten `200` teruggeven. Dit bewijst alleen HTTP-bereikbaarheid, niet
dat de simulatie of controller functioneel gezond is.

Gebruik voor de standaard-preflight:

```sh
skills/hcq-hil/scripts/hil_preflight.sh
```

Vóór de HIL-testcontroller met hostname `openquatt-test` is geflasht, geef het
huidige DHCP-adres expliciet mee:

```sh
skills/hcq-hil/scripts/hil_preflight.sh \
  --controller http://192.168.2.86/
```

Optionele afwijkende adressen:

```sh
skills/hcq-hil/scripts/hil_preflight.sh \
  --simulator http://SIMULATOR-IP/ --controller http://CONTROLLER-IP/
```

## Bedieningsvlak

De huidige ESPHome-webinterfaces zijn het geverifieerde bedieningsvlak voor
entities. Gebruik uitsluitend de expliciet opgesomde GET/POST-requests in
[`skills/hcq-hil/references/esphome-rest.md`](../skills/hcq-hil/references/esphome-rest.md)
voor geautomatiseerde entities; de browser blijft geschikt voor actuele
telemetrie en handmatige bediening. Andere entity-endpoints mogen niet worden
gegokt of brute-force verkend.

Voor een nieuwe geautomatiseerde actie: leg eerst met browserinspectie of
officiële documentatie vast welk protocol en welke authenticatie werkelijk
nodig zijn. Voeg vervolgens alleen het minimaal noodzakelijke, expliciet
geverifieerde commando aan de REST-referentie toe.


## Manual defrost capability (v0.5.0)

The base contract stays `openquatt-modbus-opentherm-v2`. Check the running
`text_sensor/ODU Defrost Contract` equals `manual-defrost-v1` before claiming
manual-cycle coverage. `text_sensor/ODU 1 defrost diagnostics` and the ODU 2
counterpart expose `started=N completed=N aborted=N` once per second. Counters
reset on boot/profile reconfiguration, not the generic diagnostics-reset button.

Use the controller's normal defrost request, not a second Modbus master. Require
actual heating/compressor/flow feedback and no fault/manual override/injected
bit. `3999=4` starts one cycle; duplicates never restart it. Default duration is
30 seconds; the existing forced-duration controls permit 1..600 seconds. Check
`2099=4`, `2118=1`, `2108 & 0x0054 == 0x0054`, and started +1; then heating,
defrost bit clear, completed +1 and unchanged aborted. Normal stop commands or
lost prerequisites abort once. Independent injected-defrost switches remain bit
fixtures. Check final physical stop feedback after a stop, not only an ACK.

This is an accelerated control-path fixture, not real ODU defrost validation.
