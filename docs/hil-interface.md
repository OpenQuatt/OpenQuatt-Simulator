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
~/.codex/skills/hcq-hil/scripts/hil_preflight.sh
```

Vóór de HIL-testcontroller met hostname `openquatt-test` is geflasht, geef het
huidige DHCP-adres expliciet mee:

```sh
~/.codex/skills/hcq-hil/scripts/hil_preflight.sh \
  --controller http://192.168.2.86/
```

Optionele afwijkende adressen:

```sh
~/.codex/skills/hcq-hil/scripts/hil_preflight.sh \
  --simulator http://SIMULATOR-IP/ --controller http://CONTROLLER-IP/
```

## Bedieningsvlak

De huidige ESPHome-webinterfaces zijn het geverifieerde bedieningsvlak voor
entities. Gebruik de browser voor instellingen, switches, buttons en actuele
telemetrie. Een generieke HTTP-GET/POST-entity-API is niet geverifieerd en mag
niet worden gegokt of brute-force verkend.

Voor een nieuwe geautomatiseerde actie: leg eerst met browserinspectie of
officiële documentatie vast welk protocol en welke authenticatie werkelijk
nodig zijn. Voeg vervolgens alleen het minimaal noodzakelijke, expliciet
geautoriseerde commando hier toe.
