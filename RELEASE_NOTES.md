# Paketkasten Firmware `v2.1.0` (Baseline: `v2.0.2`)

## Migrationsanleitung (Upgrade von `v2.0.x` / `v1.x`)

> **Kurzfassung:** Bestehende Einstellungen (WLAN, MQTT-Zugangsdaten, Wiegand-Tokens & PIN-Codes) liegen im NVS und **bleiben beim OTA-Update erhalten**. Da `v2.1.0` eine neue Motorsteuerung und eine komplett überarbeitete WebUI mitbringt, müssen **beide Dateien** geflasht und anschließend die Motor-Geschwindigkeiten einmal kurz geprüft werden.

### 1. Vor dem Update: Backup erstellen (empfohlen)
* Öffne die bisherige WebUI und sichere unter **Backup & Restore → Export Settings** deine Konfiguration als JSON-Datei.
* **Wichtig bei eigenen TLS-CA-Zertifikaten (MQTTS / HTTPS Callback):** Eigene PEM-Zertifikate liegen im **LittleFS** (`/certs/*.pem`) und werden beim Update von `littlefs-v2.1.0.bin` überschrieben. Sie sind im JSON-Export enthalten (oder vorab aus den Textfeldern kopieren) und lassen sich nach dem Update einfach wieder eintragen. *(Bei aktiviertem „Skip Certificate Validation“ ist nichts weiter nötig.)*

### 2. Update per OTA (`/update`) — Reihenfolge beachten
Lade aus diesem Release **beide** Dateien herunter und spiele sie unter `http://<paketkasten-ip>/update` nacheinander ein:
1. **Zuerst `firmware-v2.1.0.bin`** unter **Firmware** hochladen → automatischen Neustart abwarten (~10 Sekunden).
2. **Danach `littlefs-v2.1.0.bin`** unter **Filesystem** hochladen → automatischen Neustart abwarten (~10 Sekunden) und die Seite mit `Strg+F5` neu laden.

*(Hinweis: `firmware` zuerst zu flashen stellt sicher, dass bereits das gehärtete OTA mit `Update.abort()` und Minimal-OTA-Fallback aktiv ist. Für Neu-Installationen per USB/FTDI steht weiterhin `complete-firmware-v2.1.0.bin` bereit — diese überschreibt den gesamten Flash ab `0x0000`.)*

### 3. Nach dem Update: Motor-Geschwindigkeit einmalig abstimmen
In älteren Versionen (`≤ v2.0.2`) fuhr der Motor bei jedem Start fest **100 ms mit Vollgas (`200/255`)**, sodass die Schieberegler oft kaum wirkten und der Arm über die Hall-Sensoren hinausschoss.

`v2.1.0` nutzt durchgehend **Slow-Decay PWM** (aktiv bremsende PWM-Pausen), einen kurzen einstellbaren **Start Kick** und automatische richtungsabhängige Blockier-Nachregelung. Bisherige `dutyCycleOpen`-/`dutyCycleClose`-Werte fahren daher deutlich sanfter — besonders bei großen/schweren Kästen muss **Opening Speed** ggf. etwas höher und **Closing Speed** ggf. etwas niedriger gewählt werden:

* **Praxis-Richtwerte zum Start:**
  * **Kompakter / leichtgängiger Kasten:** `Opening Speed: 110–130` · `Closing Speed: 50` · `Start Kick: 20 ms`
  * **Großer / schwerer Hauptkasten:** `Opening Speed: 180–210` · `Closing Speed: 55–65` · `Start Kick: 35–45 ms`
* **Feinjustierung in 2 Minuten:**
  1. In der neuen WebUI unter **Dashboard** (oder *Settings → Motor Speed*) auf **Last movement** achten — bei Bedarf unter *Settings* kurz **Dev Telemetry Log** aktivieren (wirkt sofort ohne Neustart).
  2. Einmal **Open Parcel** und **Open Mail** auslösen:
     * Erscheint **`stall boost`** bei *Last movement*, war *Opening Speed* zu niedrig → in 10er-Schritten erhöhen (Ziel: ca. `100–400 ms` für *Parcel*, `200–500 ms` für *Mail* ohne *stall boost*).
     * Bleibt der Sensor **Closed** nach dem Schließen inaktiv (Arm rutscht hinter `Closed`), *Closing Speed* in 5er-Schritten senken (typisch `45–65`).
     * Brummt der Motor beim Anlaufen kurz, *Start Kick* um `5–10 ms` erhöhen.
  3. **Save & Reboot** klicken — fertig.

### 4. Wichtige Änderungen gegenüber `v2.0.2`
* **Auto-Kalibrierung entfernt:** Die bisherige Auto-Kalibrierung war unzuverlässig und wurde vollständig entfernt.
* **MQTT-Kompatibilität:** Bestehende Topics (`paketkasten/state`, `paketkasten/command`) und Befehle (`OPEN_PARCEL`, `OPEN_MAIL`) bleiben **100 % unverändert**. Neu hinzugekommen sind `LOCK` und `RESET_ERROR`.
* **Wiegand-Codes:** Alle bestehenden Owner- und Delivery-Codes funktionieren unverändert weiter.

---

## Changelog (`v2.0.2` → `v2.1.0`)

### Motorsteuerung & Mechanik
* **Slow-Decay PWM mit Start Kick (`0–100 ms`, 100 % Losbrechimpuls):** Gleichmäßige, gut dosierbare Fahrt ohne den alten 100-ms-Vollgas-Schlag; beim federunterstützten Schließen wird der Start-Kick automatisch halbiert.
* **Erweiterte Leistungsbereiche:** `Opening Speed` `20–255` (Default `110`), `Closing Speed` `20–180` (Default `50`), `Start Kick` `0–100 ms` (Default `20 ms`) — deckt leichte und schwere Hauptkästen ab.
* **Richtungsabhängige Stall-Nachregelung:** Bleibt ein Hall-Sensor nach `800 ms` aus, steigert die Firmware den Duty-Cycle alle `200 ms` um `+25` — beim **Öffnen bis volle `255` (100 %)** für schwere/kalte Riegel, beim **Schließen begrenzt auf `180`** zum Schutz des geschlossenen Anschlags (`3,2 s` Timeout).
* **Direktes High-Speed-LEDC (Kanäle 2 & 3, Timer 1):** Entfernt `analogWrite()` und `noInterrupts()` vollständig (behebt den Arduino-ESP32-`2.0.16`-Bug, bei dem `analogWrite()` 2.000×/s den PWM-Timer zurücksetzte und Core-1-Interrupts sperrte).
* **Aktive Bremse → DRV8871-Sleep:** Nach Erreichen eines Sensors bremst die H-Brücke `200 ms` aktiv (`255, 255`) und geht danach in Sleep/Coast (`0, 0`), während die selbsthemmende Schnecke die Position hält.
* **Hall-Sensor- & Zustands-Härtung:** Boot-Sensorerkennung, 20-µs-gefilterter Level-Failsafe zusätzlich zu den Flanken-ISRs, sofortiges Task-Wakeup aus den ISRs (`vTaskNotifyGiveFromISR`), atomare Zustandsübergänge (`stateMux`) und Schutz gegen wiederholtes Anfahren des geschlossenen Anschlags durch Autolock.
* **Fehler-Recovery & manuelle Steuerung:** Neue Aktionen **Lock / Home** (`POST /lock`, MQTT `LOCK`) und **Clear Motor Error** (`POST /reset-error`, MQTT `RESET_ERROR`) sowie Anzeige der letzten Fahrtdauer inkl. Stall-Boost-Hinweis.

### Wiegand & Zugangskontrolle (RFID-Fix)
* **Zuverlässige RFID-Token- & PIN-Erkennung beim ersten Versuch:**
  * Beseitigt Core-1-Interrupt-Sperren im Ruhezustand (`noInterrupts()`/`analogWrite()` entfernt).
  * Behebt die Unsigned-Underflow-Race-Condition in `Wiegand::loop()`, die 26-Bit-Frames im Flug abschnitt; Zeitstempel und 64-Bit-Bitshifts sind jetzt atomar unter `_wiegandMux` geschützt.
  * Echte `200 µs` Inter-Bit-Entprellung (statt fehlerhafter `50 µs`) filtert Flanken-Nachschwingen am Pegelwandler zuverlässig heraus.
  * Wiegand-ISRs laufen 100 % im **IRAM** (`esp_timer_get_time()`, kein `digitalRead()` im ISR), verwerfen isolierte Vorab-Störpulse (`> 15 ms`) automatisch und pollen alle `5 ms`.
* **AccessControl gehärtet:** 4/8-Bit-Keypad-PINs werden nicht mehr fälschlich als Hex geparst; 26/34-Bit-Codes matchen führende Nullen sauber; Einmalcodes & `deliveryBlocked` werden erst nach Prüfung von Zustand und 2,5-s-Cooldown eingelöst; W26/W34-Paritätsprüfung für Diagnose.

### Neues Responsive WebUI & Live-Telemetrie
* **Modernisierte Weboberfläche (PC, Tablet & Smartphone):** 100 % offline ohne externe Abhängigkeiten, 2-Spalten-Layout auf PC/Tablet, Touch-optimiert auf dem Smartphone, Light/Dark-Theme-Umschalter, visuelle 3-Sensor-Positionsleiste (`Closed — Parcel — Mail`), 3 aufgeräumte Tabs (*Dashboard*, *Access*, *Settings*), 1-Klick-Übernahme gescannter Wiegand-IDs (`Copy`, `+ Owner`, `+ Delivery`) und schwebende *Unsaved changes*-Save-Leiste.
* **Live Buffered Telemetry („Dev Telemetry Log“):** Statischer 48-Einträge-Ringpuffer im RAM (~5,4 KB BSS, kein Flash-Verschleiß) mit sofort zuschaltbarer Live-Konsole im Dashboard (`GET /logs`, `POST /logs/clear`, `POST /dev-mode`, *Copy Log*) für Wiegand-Frame-Timings, Hall-Sensoren, Motorfahrten, WiFi, MQTT, HTTP-Callbacks und OTA.

### Stabilität, Netzwerk, TLS & OTA
* **TLS Use-After-Free behoben:** Root-CA-Zertifikate für MQTTS und HTTPS-Callbacks behalten während der gesamten Client-Lebensdauer gültigen Speicher.
* **Asynchrone HTTP/HTTPS-Callbacks:** Ausgelagert in eigene Queue auf Core 0 (`MqttTask`, 8 KB mbedTLS-Stack, `3 s` Timeout) — blockiert nie mehr Core 1 oder Wiegand.
* **OTA & Webserver gehärtet:** Keine blockierenden `delay(2500)`-Aufrufe mehr in AsyncWebServer-Handlern, `Update.abort()` bei abgebrochenem Upload, korrekter HTTP `500` bei OTA-Fehlern, `otaInProgress`-Sperre für Motor/MQTT/Wiegand und Minimal-OTA-Fallback bei defektem LittleFS.
* **Netzwerk & Thread-Safety:** Automatischer WiFi-STA-Reconnect + periodischer SoftAP-Retry (nach Stromausfall), begrenzter MQTT-Puffer (max. 20 Einträge, kein Wachstum ohne Broker), `unsigned long`-Timer in `MqttManager::reconnect()`, Mutex-Absicherung in `ConfigManager`.

### Buzzer & Melodien
* **Direktes High-Speed-LEDC (Kanal 0, Timer 0)** statt Arduino `tone()`: kein Queue-Stau bei schnellen Noten oder erneutem Test-Klick, spart ~6 KB Heap, echte Unterstützung punktierter Noten.
* **Korrigierte Melodien:** **Tetris** (Array-Mismatch 39/40, überzählige Takt-1-Noten und fehlende punktierte Viertel behoben), **Imperial March** (`G#4` statt `G4` + echter Marsch-Rhythmus), **Mario** (Takt & Triole korrigiert), **Windows XP Startup** (echte 6-Noten-Melodie), **Intel Inside** (Piezo-Oktave & Pause).
* **Neu:** Eigener Synthwave-Jingle **Neon** (`NEON`).

### Entfernt & Qualitätssicherung
* **Entfernt:** Unzuverlässige Auto-Kalibrierung aus Firmware, LED-Status und WebUI.
* **Tests & CI:** 21 native Host-Unit-Tests (`test_motor_profile` & `test_access_control`), UTF-8-Fix in `set_ui_version.py`, Python-3.12- & `gh release`-CI-Update, aktualisierte Dokumentation.
