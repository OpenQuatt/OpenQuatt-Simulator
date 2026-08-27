# Testerhandleiding HCQ Boiler- en Quatt ODU-simulator

## 1. Doel

Deze HCQ Q-edition revision 1.0 simuleert gelijktijdig:

- één OpenTherm-cv-ketel op de `OTT`-aansluiting;
- maximaal twee onafhankelijke Quatt-ODU's op de `M2`-RS485-aansluiting.

De simulator produceert geen warmte en stuurt geen fysieke pomp, compressor of
relais aan. Alle waarden zijn softwarematig. Gebruik hem voor functionele,
protocol- en regressietests van een OpenQuatt-controller.

## 2. Aansluiten

### OpenTherm

Verbind de `OTB`-aansluiting van de controller onder test met `OTT` op de
simulator. OpenTherm is tweedraads en niet polariteitsgevoelig.

### Quatt ODU / RS485

Verbind de primaire ODU-RS485-bus van de controller met `M2` op de simulator:

| Controller | Simulator |
|---|---|
| A | A |
| B | B |
| GND | GND |

De simulator gebruikt `19200 8E1`: 19200 baud, 8 databits, even parity en één
stopbit. De onboard RS485-transceiver wordt gebruikt; een externe MAX485 is
niet nodig.

Let op:

- sluit geen echte ODU met hetzelfde Modbusadres op deze bus aan;
- gebruik terminatie alleen aan de twee uiteinden van de RS485-bus;
- verbind nooit twee OpenTherm-masteraansluitingen met elkaar.

## 3. Webinterface openen

Open in een browser:

```text
http://hcq-system-simulator.local/
```

Het huidige DHCP-adres tijdens het opstellen van deze handleiding is
`192.168.2.63`, maar dit adres kan veranderen. Gebruik daarom bij voorkeur de
hostname.

Als de simulator geen verbinding met het bekende wifi-netwerk krijgt, start
hij een access point:

```text
SSID: HCQ System Simulator
Password: openquatt
```

## 4. Normale uitgangsinstellingen

Controleer vóór een normale test:

| Instelling | Normale waarde |
|---|---|
| `Quatt ODU simulation enabled` | On |
| `ODU responses enabled` | On |
| `ODU 1 responses enabled` | On |
| `ODU 2 responses enabled` | On |
| `ODU timeout injection enabled` | Off |
| `ODU exception injection enabled` | Off |
| `ODU reboot on matching request` | Off |
| `ODU malformed response` | Disabled |
| `Drop every Nth ODU request` | 0 |
| `Modbus response delay` | 0 ms |
| `ODU fast simulation mode` | Off |
| `ODU experimental performance extrapolation` | Off |
| `ODU 1/2 force no flow` | Off |
| `ODU 1/2 freeze measured frequency` | Off |
| `ODU 1/2 defrost` | Off |
| `ODU 1/2 frequency-table preset` | Factory |

Druk vóór een nieuwe meting op `Reset ODU diagnostics`.

## 5. ODU-profielen kiezen

Per adres zijn de volgende profielen beschikbaar:

| Weboptie | Simuleert | F-levels |
|---|---|---:|
| `Disabled` | geen antwoord op dit adres | n.v.t. |
| `V1` | eerste generatie | F0-F10 |
| `V1.5` | V1.5 | F0-F10 |
| `V2 old` | oud V2-model | F0-F10 |
| `V2 new` | nieuw V2-model | F0-F20 |

Een profielkeuze is eerst alleen `pending`. Activeren gaat als volgt:

1. Kies `ODU 1 profile (pending)` en `ODU 2 profile (pending)`.
2. Controleer `ODU 1 Modbus address (pending)` en `ODU 2 Modbus address
   (pending)`. Normaal zijn dit adres 1 en 2.
3. Druk op `Apply ODU profiles and addresses, then reboot`.
4. Wacht tot de webinterface opnieuw bereikbaar is.
5. Controleer in `ODU 1 diagnostics` en `ODU 2 diagnostics` de actieve waarden,
   bijvoorbeeld `addr=1 profile=V1.5`.

Alleen de selectiewaarde wijzigen activeert het profiel dus niet. De expliciete
reboot bootst een echte busonderbreking en herdetectie na. Gelijke adressen
worden geweigerd.

Aanbevolen configuraties:

| Test | ODU 1 | ODU 2 |
|---|---|---|
| Legacy duo | V1 | V1.5 |
| V1.5 duo | V1.5 | V1.5 |
| V2-migratie / PR #534 | V2 old | V2 new |
| Nieuw V2-duo | V2 new | V2 new |
| Gemengde generatie | V1.5 | V2 new |

## 6. Controleren of OpenQuatt communiceert

Bekijk `ODU 1 diagnostics` en `ODU 2 diagnostics`. De belangrijkste velden zijn:

| Veld | Betekenis |
|---|---|
| `addr` | actief Modbusadres |
| `profile` | actief ODU-profiel |
| `mode` | 0 standby, 1 cooling, 2 heating |
| `req` | totaal ontvangen Modbusrequests |
| `read` | aantal reads |
| `write` | aantal writes |
| `drop` | bewust of onbedoeld niet beantwoorde requests |
| `exc` | verzonden Modbus-exceptions |
| `bad_addr` | niet-ondersteunde adressen |
| `bad_write` | ongeldige writes |
| `cap` | F-level boven de profielcapability |
| `maxF` | hoogste door de controller aangevraagde F-level |
| `age_ms` | leeftijd van het laatste request |
| `last` | laatste functiecode, read en write |

Een oplopende `req`-teller op beide adressen bevestigt dat de RS485-verbinding
werkt. `age_ms=4294967295` betekent dat sinds de reboot nog geen enkel request
is ontvangen.

Bij een normale test horen `drop`, `exc`, `bad_addr` en `bad_write` nul te
blijven. Een stijgende `ODU extension reads on legacy` betekent dat de
controller ten onrechte V2-extensionregisters bij V1, V1.5 of V2 old leest.

## 7. Pomp- en flowgedrag

OpenQuatt bestuurt de gesimuleerde pomp met:

| Register | Functie |
|---:|---|
| `2010 = 4096` | pomprelais aan |
| `2010 = 0` | pomprelais uit |
| `2015 = 0..1000` | gevraagde iPWM-pompsnelheid |

Alleen `2015` schrijven start de pomp niet. Het relais moet ook via `2010`
worden ingeschakeld.

Na inschakelen loopt de flow geleidelijk op volgens ongeveer:

```text
flow_lph = clamp(100 + 0,75 × iPWM, 0, 1200)
```

Bij iPWM 800 is de doelflow ongeveer 700 L/h. Zodra de flow minstens 250 L/h
is en de pomp minimaal één seconde draait, wordt de flowswitch actief.

De belangrijkste feedbackregisters zijn:

| Register | Betekenis |
|---:|---|
| `2108`, bit `0x0800` | DC-pomprelais actief |
| `2115`, bit `0x2000` | flowswitch actief |
| `2137` | pompfeedback; 20 is standby, 50..750 is normaal draaien |
| `2138` | flow; OpenQuatt rekent `raw × 0,618 L/h` |

Bij uitschakelen gaan relaisstatus, flowswitch en pompfeedback direct naar
standby. De gemeten flow loopt geleidelijk terug naar nul.

`ODU 1/2 force no flow` simuleert een pomp die wordt aangestuurd maar geen flow
levert.

## 8. Compressor, frequentie en vermogen

OpenQuatt schrijft:

| Register | Functie |
|---:|---|
| `3999` | 0 standby, 1 cooling, 2 heating |
| `1999` | fysiek F-level |

Standby of F0 geeft een doelfrequentie van 0 Hz. Heating en cooling gebruiken
ieder hun eigen frequentietabel. De gemeten frequentie loopt met een ramp naar
de doelfrequentie; een modewissel stopt de compressor eerst.

V1, V1.5 en V2 old begrenzen een aanvraag boven F10 tot F10 en verhogen de
`cap`-teller. V2 new accepteert maximaal F20; heating F20 rapporteert 110 Hz.

Het thermische vermogen en de COP zijn gesimuleerd op basis van een lokale
numerieke OpenQuatt-snapshot. Boven 90 Hz blijft de frequentietelemetrie
correct, maar vermogen en COP worden standaard op het 90Hz-punt begrensd. De
diagnose `high-frequency performance synthetic` wordt dan actief.

## 9. Frequentietabellen

`Factory` herstelt de standaardtabel voor het actieve profiel. V1 en V1.5
hebben ook de bekende preset `Runtime modified`.

Runtimewrites naar de tabellen worden standaard geaccepteerd en in RAM
bewaard. `ODU 1/2 frequency table dirty` geeft aan dat de runtimewaarde van de
factoryfixture afwijkt. Gebruik `ODU 1/2 restore factory tables` om terug te
gaan. Tabelwrites overleven een reboot niet.

## 10. Defrost testen

Zet `ODU 1 defrost` of `ODU 2 defrost` aan. Dan:

- wordt register `2118` gelijk aan 1;
- wordt de defroststatusbit actief;
- kan de water-outtemperatuur tijdelijk dalen;
- blijven de bestaande compressorlevel en frequentie standaard behouden.

Met `Hold physical level during defrost` aan wordt een nieuw F-level tijdens
defrost geregistreerd, maar pas na beëindigen van defrost toegepast. Dit is
bedoeld om de scheiding tussen logisch modellevel en fysiek F-level te testen.

## 11. Faults injecteren

Per ODU kunnen de ruwe faultwoorden `2119`, `2120` en `2121` worden ingevuld.
Waarden zijn decimaal in de webinterface; meerdere bits kunnen worden
gecombineerd door de bitwaarden op te tellen.

Voorbeelden:

| Scenario | Register | Decimale waarde | Hex |
|---|---:|---:|---:|
| Compressor oil return | 2119 | 8 | `0x0008` |
| High-pressure switch lock | 2120 | 64 | `0x0040` |
| DC-waterpump fault | 2121 | 8192 | `0x2000` |

Zet de faultwoorden na de test terug op 0. De simulator implementeert momenteel
geen automatische clear of reboot-latched faultstatus.

## 12. Protocolfouten injecteren

Gebruik protocolinjecties altijd één voor één en zet ze na de test weer uit.

| Test | Instelling |
|---|---|
| Hele ODU-bus offline | `ODU responses enabled` Off |
| Eén adres offline | `ODU 1/2 responses enabled` Off |
| Vaste vertraging | `Modbus response delay` |
| Iedere Nde response verliezen | `Drop every Nth ODU request` |
| Timeout op specifiek startadres | `ODU timeout injection enabled` + startadres |
| Exception op specifiek startadres | exception enable + startadres + code |
| Verkeerde bytecount | malformed response + startadres |
| Incomplete response | malformed response + startadres |
| Reboot tijdens een read | reboot-on-matching-request + startadres |

Voor een V2-extensionfailure is startadres `3050` de logische keuze. Voor een
reboot tijdens de basistabelread is `3000` de standaard.

## 13. OpenTherm-ketelsimulator

De OpenTherm-simulator werkt onafhankelijk van de ODU-simulator.

In de normale stand staat `Automatic boiler model` aan. De simulator gebruikt
dan de ontvangen masterstatus en `TSet` voor ignition, flame, modulation en
water-/retourtemperatuur.

Voor handmatige telemetrie:

1. Zet `Manual telemetry` aan.
2. Stel de gewenste boiler-, retour- en modulatiegegevens in.
3. Gebruik de handmatige status- en faultschakelaars.

Bij normale OpenTherm-belasting moeten de volgende tellers nul blijven:

- `OpenTherm RX queue overflow count`;
- `OpenTherm response queue failure count`;
- `OpenTherm response overlap count`;
- `OpenTherm TX error count`.

`OpenTherm response queued count` en `OpenTherm TX completed count` horen op
langere termijn gelijk op te lopen.

## 14. Aanbevolen basistest

1. Verbind OpenTherm en RS485 volgens hoofdstuk 2.
2. Zet alle foutinjecties uit en herstel de factorytabellen.
3. Kies de gewenste ODU-profielen en voer Apply/reboot uit.
4. Druk op `Reset ODU diagnostics`.
5. Start of herstart de controller onder test.
6. Controleer dat beide `req`-tellers oplopen en beide profielen correct worden
   gedetecteerd.
7. Schakel de pomp in en controleer relaisstatus, flow, flowswitch en feedback.
8. Test standby, heating, cooling, F0, F10 en voor V2 new ook F20.
9. Test defrost en minimaal één faultwoord.
10. Test verlies en herstel van één ODU-adres.
11. Controleer dat alle onverwachte errorcounters nul zijn gebleven.
12. Herhaal de relevante stappen terwijl OpenTherm tegelijk actief wordt
    gepolld.

## 15. Wat rapporteren bij een probleem

Noteer of maak screenshots van:

- gekozen profiel en Modbusadres per ODU;
- alle protocolinjectie-instellingen;
- `ODU 1 diagnostics` en `ODU 2 diagnostics`;
- requested en accepted physical level;
- demand en measured frequency;
- flow, thermal power en electrical power;
- de drie faultwoorden;
- de relevante OpenTherm-tellers;
- simulator-IP, firmwareversie en tijdstip van de test;
- de exacte handeling waarna het probleem ontstond.

Reset de diagnostiek niet voordat deze informatie is vastgelegd.

## 16. Beperkingen

- De simulator bewijst geen werkelijk hydraulisch, thermisch of elektrisch
  ODU-gedrag.
- Temperaturen, drukken, fan-, EEV- en delen van het vermogensmodel zijn
  synthetisch maar intern consistent.
- V2-performance boven 90 Hz is niet gevalideerd.
- Het grootste deel van de EEPROM-metadata is een synthetische fixture.
- Runtimefrequentietabellen zijn RAM-only.
- Een succesvolle gesimuleerde write bewijst niet dat echte hardware dezelfde
  write accepteert of persistent opslaat.
- Eindvalidatie op echte ODU-hardware blijft noodzakelijk.
