# HCQ desktop HIL-lab

Dit document is de operationele bron voor HIL-tests met de vaste
desktopopstelling. De simulator en controller hebben ieder een eigen firmware
en mogen alleen via de beschreven bussen met elkaar verbonden zijn.

## Apparaten

| Rol | URL | Verwachte firmware |
|---|---|---|
| HCQ simulator | `http://192.168.2.63/` | OpenQuatt Simulator, contract `openquatt-modbus-opentherm-v1` |
| Testcontroller | `http://192.168.2.86/` | OpenQuatt testcontroller |

De IP-adressen zijn DHCP-adressen. Als een adres wijzigt, pas deze tabel en
`docs/hil-interface.md` samen aan voordat een testcase wordt uitgevoerd.

## Topologie

```text
Testcontroller OTB (master) ───── HCQ simulator OTT (ketel/slave)
Testcontroller OTT (slave)  ───── HCQ simulator OTB (thermostaat/master)
Testcontroller primaire ODU ───── HCQ simulator M2 (RS485, 19200 8E1)
```

De twee OpenTherm-lussen zijn elektrisch gescheiden. Elke kabel verbindt exact
één master met één slave. Verbind nooit de twee simulatorpoorten met elkaar,
twee masters of twee slaves. Sluit geen echte ODU met hetzelfde adres op M2
aan.

## Baseline vóór iedere testcase

1. Voer de read-only preflight uit volgens `hil-interface.md`.
2. Controleer op de simulator de contract- en versie-entity.
3. Controleer op de controller dat beide OpenTherm-links gezond zijn.
4. Zet foutinjecties uit, herstel eventuele tijdelijke ODU-instellingen en
   noteer de beginwaarden van relevante counters.
5. Leg de testcase, tijd en gewenste eindtoestand vast.

## Muterende acties

Alleen na expliciete gebruikersopdracht of een expliciet goedgekeurde testcase:

- simulator- of controllerinstellingen aanpassen;
- CH/DHW-vraag, kamertemperatuur, setpoint of ODU-profiel aanpassen;
- foutinjectie, timeout, exception of responsonderdrukking inschakelen;
- reboot, OTA of firmwarewisseling.

Een geslaagde HTTP- of UI-actie is geen testresultaat. Bevestig altijd het
bedoelde effect in actuele controllertelemetrie en de relevante simulator- en
controllercounters.

## Resultaatformat

Rapporteer per testcase: `PASS`, `FAIL` of `BLOCKED`; start/eindtoestand;
uitgevoerde acties; verwacht en gemeten resultaat; firmwareversies; relevante
counters; en screenshots of andere bewijsgegevens. Zet de opstelling daarna
terug naar baseline, tenzij de gebruiker anders vraagt.
