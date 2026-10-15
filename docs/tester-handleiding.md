# Testerhandleiding HCQ Boiler- en Quatt ODU-simulator

Deze handleiding hoort bij OpenQuatt Simulator `v0.4.0`, contract
`openquatt-modbus-opentherm-v2`. Controleer beide entities vóór een HIL-run;
een afwijkend contract betekent dat de runner en simulator niet aantoonbaar
compatibel zijn.

## 1. Doel

Deze HCQ Q-edition revision 1.0 simuleert gelijktijdig:

- één OpenTherm-cv-ketel op de `OTT`-aansluiting;
- één OpenTherm-thermostaat op de `OTB`-aansluiting;
- maximaal twee onafhankelijke Quatt-ODU's op de `M2`-RS485-aansluiting.

De simulator produceert geen warmte en stuurt geen fysieke pomp, compressor of
relais aan. Alle waarden zijn softwarematig. Gebruik hem voor functionele,
protocol- en regressietests van een OpenQuatt-controller.

## 2. Aansluiten

### OpenTherm

Verbind de `OTB`-aansluiting van de controller onder test met `OTT` op de
simulator. OpenTherm is tweedraads en niet polariteitsgevoelig.

Voor een thermostaatsimulatie is de richting precies andersom: verbind `OTT`
van de controller met `OTB` op de simulator. De twee OpenTherm-aansluitingen
van de HCQ v1.0 zijn elektrisch gescheiden en kunnen tegelijk worden gebruikt.

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
- elke OpenTherm-kabel verbindt exact één master met één slave; verbind nooit
  twee masters, twee slaves of beide simulatorpoorten met elkaar.

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
| `ODU external system pump flow` | Off |
| `ODU experimental performance extrapolation` | Off |
| `ODU 1/2 manual telemetry override` | Off |
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

Schakel `ODU external system pump flow` in wanneer de geteste controller een
aparte systeempomp aanstuurt en register `2010` daarom niet gebruikt. De
simulator past dan de iPWM-flow toe zonder het ODU-pomprelais te vereisen.

Quatt-iPWM werkt omgekeerd: een lagere waarde stuurt de pomp harder aan. Na
inschakelen loopt de flow geleidelijk naar ongeveer:

```text
flow_lph = clamp(950 - iPWM, 0, 1200)
```

Bij iPWM 150 is de doelflow ongeveer 800 L/h; bij iPWM 800 ongeveer 150 L/h.
Zodra de flow minstens 250 L/h is en de pomp minimaal één seconde draait,
wordt de flowswitch actief.

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

Voor koude-starttests is `ODU 1/2 water-in temperature` instelbaar van 0,0 tot
60,0 °C in stappen van 0,1 °C.

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
`cap`-teller. V2 new accepteert maximaal F20; zonder de optionele
verwarmingsbegrenzing rapporteert heating F20 een doel van 110 Hz.

Het thermische vermogen en de COP zijn gesimuleerd op basis van een lokale
numerieke OpenQuatt-snapshot. V2 gebruikt de volledige CiC 4.2.0-surface;
gemaskeerde punten en waarden buiten het V2-temperatuurdomein leveren geen
synthetisch vermogen. Boven 90 Hz blijft de frequentietelemetrie correct,
maar vermogen en COP worden standaard op het 90Hz-punt begrensd. De diagnose
`high-frequency performance synthetic` wordt dan actief.

### Optionele verwarmingsbegrenzing V2 new

Schakel `ODU 1/2 heating limits enabled` in om per ODU een beperkt
V2-new-verwarmingsprofiel te testen. De switches starten na boot uit. Het
profiel werkt alleen tijdens normaal verwarmen van V2 new; bij andere
varianten, cooling en defrost blijft het bestaande contractmodel actief.
De buiteninstelling loopt nu van -30 tot 50 °C, zodat ook de koude en warme
uitschakel-/herstartgrenzen bedienbaar zijn. Dit vergroot het gevalideerde
thermische performance-domein niet.

De ontvangen fysieke stand blijft ongewijzigd in `accepted physical level`
en register `1999`. De begrenzing kiest een `prepared physical level` en
zoekt die op in de actuele verwarmingstabel. Een aangepaste tabel verandert
daarom ook de Hz van de begrensde stand. `heating limit reasons` vermeldt
alle van toepassing zijnde redenen, ook als één daarvan al een lager plafond
oplegt. `heating limits active` betekent dat het profiel wordt toegepast;
het betekent niet dat de aanvraag daadwerkelijk wordt verlaagd.

Voor een nieuw begonnen verwarmingsscenario met de standaardtabel en stilte uit:

| Buiten | Waterinlaat | Ontvangen stand | Voorbereide stand | Frequentieverzoek |
|---:|---:|---:|---:|---:|
| 7 °C | 30 °C | F20 | F14 | 76 Hz |
| 20 °C | 30 °C | F17 | F12 | 68 Hz |
| -10 °C | 33 °C | F20 | F20 | 110 Hz |
| -10 °C | 36 °C | F20 | F18 | 95 Hz |
| -10 °C | 33 °C, stilte aan | F20 | F14 | 76 Hz |

De buitenbanden hebben zonder voorgeschiedenis deze plafonds:

| Buiten | Maximale fysieke stand |
|---|---:|
| ≤ -25 °C | F0 |
| > -25 t/m -4 °C | F20 |
| > -4 t/m 1 °C | F18 |
| > 1 t/m 6 °C | F16 |
| > 6 t/m 12 °C | F14 |
| > 12 t/m 20 °C | F12 |
| > 20 t/m 24 °C | F10 |
| > 24 t/m 28 °C | F8 |
| > 28 t/m 46 °C | F6 |
| > 46 °C | F0 |

De F20-buitenband blijft actief tot en met -3 °C, maar stopt bij -25 °C.
De overige draaiende banden houden hun vorige plafond binnen 1 °C aan beide
zijden van de band. Na koude uitschakeling komt het profiel pas boven -20 °C
vrij; na warme uitschakeling pas onder 45 °C. Een temperatuursprong buiten
de hysterese kiest direct de bijbehorende nieuwe band.

F19/F20 vereisen daarnaast een vrijgave: buiten -12 t/m -1 °C en waterinlaat
≤ 34 °C. De vrijgave blijft actief tot buiten < -13 of > 0 °C, of waterinlaat
> 35 °C. Zonder vrijgave is het plafond F18. Stille mode, ontvangen via
register `2006`, begrenst op F14; `quiet mode requested` toont deze vlag.

De diagnostiek toont vier afzonderlijke grootheden:

1. `requested table frequency`: tabel-Hz van de ontvangen, geaccepteerde stand.
2. `demand frequency` / register `2102`: Hz van de voorbereide stand.
3. `driver frequency`: doel na eventuele aanvullende labderating.
4. `measured frequency` / register `2103`: frequentie na de bestaande lineaire ramp,
   of de handmatige readback wanneer de telemetriefixture actief is.

Buiten dit optionele profiel behoudt `2102` de bestaande uitlezing inclusief
labderating. Voor de standaardinstellingen zijn vraag en driverdoel gelijk.
Tijdens standby of afbouw kan de vraag al nul zijn terwijl de gemeten Hz nog
afnemen. Ongeldige temperatuurwaarden geven in dit labprofiel nulvraag en
de reden `invalid temperature`; dit is geen nagebootste sensorfoutcode.

Profielactivering, standby, F0, een ongeldige tabel, uitschakelen van de limieten en defrost wissen
de band- en vrijgavegeschiedenis bij de modelupdate. Na defrost begint de
verwarmingsbegrenzing opnieuw. Een pauze via `ODU simulation enabled` bevriest
het model, inclusief deze geschiedenis. Instelbare start-/ramp-/stoptijden
blijven synthetisch. Autonome defrost en verdere compressorbeveiligingen zijn
hiermee niet geïmplementeerd; boven 90 Hz blijft de performanceberekening
als synthetisch gemarkeerd.

Defrostmethode en startinterval worden bij profielactivering hersteld:
V1 gebruikt methode 0 en raw interval 0, V1.5 methode 0 en interval 45,
beide V2-profielen methode 4 en interval 45. Andere defrostwaarden blijven
synthetisch. Bodemplaatmode 0 wist de heaterbit ook tijdens forced defrost;
modes 1..3 behouden de synthetische temperatuur-/defrostregel en gebruiken
nog geen volledige temperatuurhysterese of heatervermogensberekening.

## 8. Handmatige telemetriefixture

Schakel `ODU 1/2 manual telemetry override` alleen in voor een afgebakende
HIL-fixture. Dan zijn de readbacks voor `2099`, `2100`, `2101`, `2105`,
`2108`, `2133`, `2134`, `2137` en `2138` exact de bijbehorende `manual`-
entiteiten. De dynamische simulator blijft intern doorlopen; de override
vervangt uitsluitend deze Modbus-reads. Schakel de override na afloop uit.

`manual operating status 2108` is een raw 16-bits woord. De
bodemplaatverwarming is bit `0x0004`; defrost staat afzonderlijk in register
`2118` en blijft onafhankelijk van de handmatige statusreadback.

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

De ingestelde faultwoorden zijn expliciete injecties. Daarnaast genereert de
communicatiewatchdog zelf `2121.b0`; dat bit verdwijnt pas na herstelde ontvangst
en een volgende watchdog-aanroep. Een nog actieve handmatige injectie van dit
bit blijft staan. De overige foutbits worden door de watchdog niet gewist.

### 11.1 Communicatiewatchdog

Iedere ingeschakelde ODU heeft een eigen teller met een nominale stap van 10 ms
en grens 12.000. De vergelijking gebeurt vóór het ophogen: op 120.000 ms bereikt
de teller de grens; bij de volgende nominale aanroep (120.010 ms) ontstaat de
communicatiefout. De teller verzadigt. De fase van de laatste aanvraag kan het
interval ongeveer één tick verschuiven.

Een correct geadresseerd en CRC-geldig FC03-, FC06- of FC16-request reset de
teller vóór registerbereikvalidatie. Ook een read of een request waarvan het
antwoord verloren gaat houdt de ODU dus actief. Een verkeerd unitadres,
broadcastadres, ongeldige CRC of FC04 reset deze normale watchdog niet.

Bij timeout wordt `2121.b0` actief. De volgende normale regelupdate wist
gevraagde modus `3999`, level `1999` en interne Hz-vraag, inclusief een eventueel
uitgesteld defrostlevel. De gemeten compressorfrequentie bouwt via het bestaande
stopmodel af; zij wordt niet direct nul gemaakt. Een hervatte read reset de
teller, maar wist de fout pas bij de volgende watchdog-aanroep en herstelt geen
gewiste opdrachten. Voor herstart zijn opnieuw regelopdrachten nodig.

Voor een toekomstige HIL-case:

1. Breng de ODU met geldige opdrachten in verwarming en volg modus, level, Hz,
   faultwoord `2121` en `wd` in `ODU 1/2 diagnostics`.
2. Onderbreek daadwerkelijk alle requests naar die ODU. Alleen antwoorden
   uitschakelen of alleen writes stoppen toetst de ontvangstwatchdog niet.
3. Controleer na de grens het foutbit en de gewiste vraag, en daarna de Hz-afbouw.
   Andere foutbits moeten behouden blijven.
4. Hervat alleen reads. Controleer dat de fout herstelt, terwijl modus/level nul
   blijven en de compressor niet vanzelf herstart.
5. Test apart dat polling zonder writes en verlies van antwoorden de watchdog
   blijven voeden, en dat verkeer naar HP2 de teller van HP1 niet reset.

De hosttest `quatt_odu_communication_watchdog_test.cpp` gebruikt een bestuurbare
klok voor de exacte grens, tellerverzadiging, ontvangstfilters, Duo, millis-rollover,
defrost en beide interleavings van een request op de timeoutgrens. De firmware
ontvangt alleen CRC-gevalideerde frames uit de RTU-parser. Ruwe UART/CRC-injectie
op hardware is geen onderdeel van deze hosttest.

Het model volgt statische analyse van één firmwarebackup. Het is een gemeenschappelijk
HIL-preset; werkelijke timing en andere firmwareversies zijn niet fysiek bevestigd.
De echte ODU kan door schedulervertraging later reageren. Noodpomplogica voor
pomptype 2 en afwijkend servicebedrijf zijn niet toegevoegd; pomp/flow blijven
het bestaande simulatiemodel volgen.

## 12. Protocolfouten injecteren

Gebruik protocolinjecties altijd één voor één en zet ze na de test weer uit.

| Test | Instelling |
|---|---|
| Geen antwoorden op de ODU-bus | `ODU responses enabled` Off |
| Geen antwoorden van één adres | `ODU 1/2 responses enabled` Off |
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

## 14. OpenTherm-thermostaatsimulator

De thermostaatsimulator is een OpenTherm-master op de `OTB`-aansluiting. Hij
stuurt `Status`, `TSet`, `TrSet` en `Tr`; standaard één bericht per seconde.
Gebruik hem alleen op de `OTT`-aansluiting van de controller onder test.

Gebruik in de webinterface:

- `Thermostat CH demand` voor de CH-bit in `Status`;
- `Thermostat DHW demand` voor de DHW-bit in `Status`;
- `Thermostat TSet` voor de gewenste aanvoertemperatuur;
- `Thermostat room setpoint` voor `TrSet` (ID 16);
- `Thermostat room temperature` voor `Tr` (ID 24);
- `Thermostat poll interval` voor de mastercyclus.

`Controller CH active via thermostat`, `Controller DHW active via thermostat`,
`Controller flame via thermostat` en `Controller fault via thermostat` tonen
de statusbits die de controller op de betreffende vraag terugstuurt.

Voor een geldige verbinding lopen `Thermostat OpenTherm requests` en
`Thermostat OpenTherm responses` op. `Thermostat OpenTherm timeouts`,
`invalid responses`, `RX queue overflow` en `TX errors` blijven nul. Start een
nieuwe run met `Reset thermostat diagnostics`.

## 15. Aanbevolen basistest

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

## 16. Wat rapporteren bij een probleem

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

## 17. M2 UART-foutinspuiting

Gebruik dit alleen voor een controllerwijziging die UART parity- of
framingfouten op de M2-RS485-bus moet afhandelen. De functie is standaard uit
na elke reboot.

1. Zet beide ODU-profielen op de normale testprofielen en bevestig dat de
   controller beide ODU's leest.
2. Reset `ODU diagnostics` en noteer de beginwaarden van de vier `M2 UART`
   diagnosetellers.
3. Schakel `M2 UART fault injection enabled` in.
4. Druk precies één keer op `Inject M2 UART parity error` of `Inject M2 UART
   framing error`.
5. Wacht tot de bijbehorende `faults injected`-teller één hoger staat en
   `M2 UART fault injection active` weer uit is. `M2 UART fault restore
   errors` moet nul blijven.
6. Controleer dat de normale M2-requests daarna weer oplopen en de controller
   verbinding houdt of volgens de geteste herstelstrategie herstelt.
7. Schakel `M2 UART fault injection enabled` weer uit.

De parity-actie verstuurt één byte met oneven parity op de verder `19200 8E1`
bus. De framing-actie verstuurt één byte gevolgd door een UART BREAK. De
simulator wacht op een idle bus, neemt de RS485-zender tijdelijk exclusief over
en herstelt daarna even parity en receive-mode. Deze test bewijst pas de
controllerfiltering wanneer de controller zelf een UART-foutmelding of
diagnostiek vastlegt; de simulatorteller bewijst alleen dat de fout is
uitgezonden.

De functie is alleen beschikbaar op M2. Voor M1 is een tweede fysiek
aangesloten RS485-lijn nodig.

## 18. Beperkingen

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
