# TODO – WS2812 Wetterstation

Hardware: Adafruit Feather STM32F405, OLED FeatherWing 128x64 (SH1107), LPS22, HTU2x, BMP180 (optional), WS2812-LEDs (12 Stück, PA6 / TIM3).

## Erledigt

- [x] WS2812-Treiber: DMA-Puffer statisch, Warten auf vorherigen Transfer, Buffer Overflow in `WS2812_Reset` behoben
- [x] Build-Probleme behoben (alter CMake-Cache, GCC-14-Fehler in `calculatePulseWidth`)
- [x] `build/` und `Debug/` aus Git entfernt, `.gitignore`, `compile_commands.json` + `.clangd`
- [x] `flash_dfu.bat` mit lokalem dfu-util und relativem Pfad
- [x] I2C1 (PB6/PB7, 400 kHz) mit Bus-Scan
- [x] LPS22: Druck + Temperatur, Adress-Erkennung 0x5C/0x5D
- [x] BMP180 / BMP280 / BME280: automatische Erkennung über die Chip-ID
- [x] HTU2x: Temperatur + Feuchte, CRC-Prüfung, Temperaturkompensation
- [x] Alle Sensoren nicht blockierend, Neusuche nach Ausfall
- [x] SH1107-Display: Font, große Schrift, Sonderzeichen (°, Pfeile)
- [x] Display-Seiten: Messwerte + Trend, Sensor 2, Feuchte + Taupunkt, Temperatur-Verlauf, Min/Max
- [x] Taster A/B/C: Seiten blättern, Min/Max zurücksetzen, Display aus/an
- [x] Temperatur-Ampel auf den LEDs mit Hysterese und Glättung
- [x] Wettertrend aus der Druckänderung über 3 Stunden
- [x] Regenbogen-Lauflicht bei Luftfeuchte > 60 % (Lüften)
- [x] Ausgabe über USB-CDC, Statusübersicht beim Öffnen des Terminals
- [x] Display: Wartezeit nach dem Einschalten, Neu-Initialisierung bei Fehlern, Selbsttest

## Offene Features

### Bedienung
- [x] **USB-Befehle** im Terminal: `help`, `status`, `set low|high|wet`, `reset`
- [x] **`dfu`-Befehl**: per USB in den Bootloader springen, ohne BOOT0 umzustecken
- [x] **Einstellungen im Flash speichern** (Schwellwerte, Helligkeit, letzte Display-Seite)
- [ ] Schwellwerte über die Taster einstellen (Einstellungs-Seite)
- [ ] Helligkeit der LEDs per Poti (ADC PC5) oder Taster (per USB schon: `set bright`)

### Display
- [x] Bildschirmschoner: Display nach X Minuten ohne Tastendruck dimmen/aus (OLED-Einbrennen)
- [ ] Nachtmodus: LEDs und Display nachts dunkler (braucht RTC mit Uhrzeit)
- [ ] Feuchte-Verlauf als Diagramm
- [ ] Druck-Verlauf über 24 Stunden als Diagramm

### Lüften
- [x] Lüft-Assistent: Fenster offen erkennen, „Fenster kann zu“ melden, 15 min Pause danach
- [x] Anzeige, wie lange schon gelüftet wird
- [x] Schimmelrisiko aus Temperatur, Feuchte und Wandtemperatur (Schätzung) berechnen und warnen
- [x] BMP180/BMP280 als Wandsensor: gemessene statt geschätzte Wandtemperatur (`set wallsensor`)

### Wetter
- [ ] Sturmwarnung: Druckabfall > 3 hPa in 3 h, LEDs pulsieren rot
- [ ] Höhe über Meer aus dem Luftdruck

### Daten und PC
- [ ] Messwerte als CSV über USB ausgeben
- [ ] Live-Dashboard am PC (Python, Vorlage: `Barometer/Tools/dashboard.py`)
- [ ] Datenlogger auf der microSD (Code aus dem Barometer-Projekt)
- [ ] Trend und Verlauf über einen Reset hinweg erhalten (Flash oder SD)

### Robustheit
- [ ] Watchdog (IWDG) für den Dauerbetrieb
- [ ] Onboard-NeoPixel (PC0) als Status-LED: grün = alles ok, rot = Sensor fehlt
- [ ] Gamma-Korrektur als Tabelle statt `pow()` in `WS2812_SetColor`

## Bekannte Baustellen

- [ ] **CubeMX:** I2C1 ist nur im Code aktiviert, nicht in der `.ioc`. Beim Neugenerieren werden `stm32f4xx_hal_conf.h` (I2C wieder aus) und `cmake/st-project.cmake` (neue Dateien fehlen) überschrieben. Besser: I2C1 auf PB6/PB7 in der `.ioc` aktivieren und dann `I2C_Bus_Init()` entfernen.
- [ ] **Display-Selbsttest** (`DISPLAY_SELFTEST` in `app.c`) ist noch aktiv und verlängert den Start um ~2 s. Testen, ob es mit `0` zuverlässig startet, sonst `DISPLAY_POWERUP_MS` erhöhen.
- [ ] **BMP180** wurde zuletzt nicht gefunden (0x76/0x77): Verkabelung prüfen.
- [ ] Restliche UART-Ausgaben (Startmeldung in `main.c`, `WS2812_Init`) auch auf das Log-Modul umstellen.
- [ ] Taster-Pins (PB8/PC6/PC7) stammen aus dem Adafruit-Pinout, am Board bestätigen.
- [ ] Ungenutzte Altlasten in `main.c` aufräumen (TIM14/RNG/ADC-Callbacks, `DMA_FINISH`, `Trigger`).
