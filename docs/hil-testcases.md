# HCQ HIL-testcases

Elke testcase begint met de baseline uit `hil-lab.md` en eindigt met herstel
naar die baseline, tenzij anders vermeld.

## OT-THERM-001 — Thermostaatwaarden ontvangen

**Doel:** de controller ontvangt de vier masterberichten van de simulator.

1. Open simulator en controller in de browser.
2. Noteer simulatorversie, controllerfirmware en de beginwaarden van de
   OpenTherm-foutcounters.
3. Stel op de simulator in: `Thermostat room setpoint` = 21 °C en
   `Thermostat room temperature` = 20 °C.
4. Wacht maximaal 15 seconden.
5. Controleer op de controller: thermostaatlink OK, actuele status, Control
   setpoint, Room setpoint = 21 °C en Room temperature = 20 °C.
6. Controleer dat simulator-requests en -responses oplopen en timeout,
   invalid response, RX-overflow en TX-errors nul blijven.

**Pass:** beide roomwaarden zijn actueel en binnen 0,1 °C van de ingestelde
waarde; links en counters zijn gezond.

## OT-THERM-002 — CH-vraag

**Doel:** de controller verwerkt de CH-bit van de thermostaatsimulator.

1. Noteer uitgangsstatus en counters.
2. Schakel `Thermostat CH demand` in op de simulator.
3. Controleer binnen 15 seconden de actuele thermostaatstatus op de controller
   en de verwachte regelreactie volgens de actieve controllerstrategie.
4. Schakel de vraag weer uit en controleer herstel.

**Pass:** status verandert en herstelt; geen onverwachte OpenTherm-fouten.

## ODU-BASE-001 — ODU-communicatie basis

**Doel:** beide virtuele ODU's zijn bereikbaar op M2.

1. Controleer de ODU-profielen en adressen op de simulator.
2. Herstart de controller alleen na expliciete opdracht indien detectie dat
   vereist.
3. Controleer dat per actief ODU-adres requests oplopen, het gekozen profiel
   wordt herkend en onverwachte protocolcounters nul blijven.

**Pass:** de controller leest beide actieve ODU's zonder protocolfouten.

## M2-UART-001 — UART parity- en framingfilter

**Doel:** de controller verdraagt fysieke UART-fouten op de M2-RS485-lijn en
herstelt normale ODU-communicatie.

1. Voer `ODU-BASE-001` uit. Reset daarna de ODU-diagnostiek op de simulator.
2. Noteer op simulator en controller de beginwaarden, firmwareversies en
   relevante UART/Modbus-logs.
3. Schakel `M2 UART fault injection enabled` in.
4. Druk eenmaal op `Inject M2 UART parity error`. Wacht tot de parity-teller
   precies één hoger staat, `M2 UART fault injection active` uit staat en
   `M2 UART fault restore errors` nul is.
5. Controleer dat M2-requests weer oplopen en leg de controllerdiagnostiek en
   eventuele reconnect vast.
6. Herhaal stap 4 en 5 met `Inject M2 UART framing error`.
7. Schakel de injectieswitch uit en controleer nogmaals normale M2-operatie.

**Pass:** beide injectietellers stijgen exact eenmaal, er zijn geen
restore-errors en de controller hervat M2-communicatie. Wanneer de te testen
controllerfirmware een UART-error-counter of logregel heeft, moet die voor
beide fouttypes zichtbaar zijn. Zonder die controllerwaarneming is de
foutinspuiting en het herstel bewezen, maar niet de interne controllerfilter.

**Herstel:** injectieswitch uit; beide ODU-profielen actief; geen lopende
injectie.

## Nieuwe testcase toevoegen

Gebruik: identifier, doel, beginstaat, expliciet gemarkeerde mutaties,
stappen, observaties, timeout, pass/fail-criteria, herstel en bewijs.
