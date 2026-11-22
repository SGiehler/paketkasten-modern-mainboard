# Paketkasten Firmware `v2.2.0`

## Neu in `v2.2.0`

### 1-Klick-Modell-Templates für alle DHL Paketkasten **Modern** Varianten
Unter **Settings → Mailbox Model Template** lassen sich jetzt praxiserprobte Vorlagen für alle Bauformen des **DHL Paketkasten Modern** (Wandmontage & freistehend, mit oder ohne Briefschlitz) mit einem Klick übernehmen:

| Template | Paketfach-Volumen (Innenmaß) | Opening Speed | Closing Speed | Start Kick | Brieffach |
|---|---|---:|---:|---:|---|
| **Modern L** | ~78 L (ca. 53 × 45 × 31 cm) | `110` | `50` | `20 ms` | Nein (nur Paket) |
| **Modern L + Mail** | ~78 L (ca. 53 × 45 × 31 cm) | `140` | `55` | `25 ms` | Ja |
| **Modern XL** | ~166 L (ca. 47 × 70 × 40 cm) | `180` | `55` | `35 ms` | Nein (nur Paket) |
| **Modern XL + Mail** | ~166 L (ca. 47 × 70 × 40 cm) | `195` | `60` | `40 ms` | Ja |

* Alle Schieberegler lassen sich danach weiterhin individuell feinjustieren (das Preset-Badge wechselt dann automatisch auf **Custom**).

### Neue Option: **Mail Compartment (Brieffach)** (`hasMailCompartment`)
* Neue Einstellung in **Settings → Behavior & Audio** (gespeichert im NVS, Standard `an` — 100 % abwärtskompatibel zu bestehenden Konfigurationen und JSON-Backups).
* **Für Kästen ohne Briefschlitz (deaktiviert):**
  * Das Dashboard blendet die überflüssige **Open Mail**-Schaltfläche aus (**Open Parcel** nutzt die volle Breite).
  * **Owner-Codes** öffnen direkt das Paketfach (`PARCEL_OPEN`) statt unnötig bis zum Brief-Sensor durchzufahren (~3× kürzerer Fahrweg, weniger Getriebeverschleiß) — setzen aber aktive *One-Time-Opening*-Sperren weiterhin sauber zurück.

### Dokumentation
* Aktualisierte `v2.1.0+` WebUI-Screenshots (Dashboard, Access, Settings) in `README.md` und Bedienungsanleitung (`doc/manual/manual.md`).

---

## Upgrade- & Migrationsanleitung

### Update von `v2.1.0` → `v2.2.0`
1. Unter `http://<paketkasten-ip>/update` zuerst **`firmware-v2.2.0.bin`** (Firmware) hochladen und Neustart abwarten.
2. Danach **`littlefs-v2.2.0.bin`** (Filesystem) hochladen, Neustart abwarten und Seite mit `Strg+F5` neu laden.
3. Alle bestehenden Einstellungen bleiben erhalten (`Mail Compartment` startet standardmäßig auf *An* und kann bei Bedarf unter *Settings* per Template umgestellt werden).

### Upgrade von älterer Firmware (`≤ v2.0.2`) → `v2.2.0`
1. **Backup sichern:** In der alten WebUI unter *Backup & Restore → Export Settings* die JSON-Datei sichern. *(Eigene TLS-CA-Zertifikate liegen im LittleFS und müssen nach dem Filesystem-Update ggf. wieder eingefügt werden; WLAN, MQTT-Passwörter und Wiegand-Codes liegen im NVS und bleiben erhalten.)*
2. **Beide Dateien per `/update` flashen:** Erst **`firmware-v2.2.0.bin`**, Neustart abwarten, dann **`littlefs-v2.2.0.bin`**, Neustart abwarten (`Strg+F5`).
3. **Modell-Template wählen:** Unter **Settings → Mailbox Model Template** das passende Modell (**Modern L / L + Mail / XL / XL + Mail**) anklicken → **Save & Reboot** → fertig! *(Bei Bedarf anschließend anhand von „Last movement“ feinjustieren; die alte Auto-Kalibrierung wurde zugunsten von Slow-Decay PWM entfernt.)*
