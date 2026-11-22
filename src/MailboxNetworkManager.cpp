#include "MailboxNetworkManager.h"
#include "ConfigManager.h"
#include "MelodyPlayer.h"
#include "SwitchManager.h"
#include "WiegandManager.h"
#include "MqttManager.h"
#include "TelemetryLog.h"
#include "state.h"
#include "MotorDriveProfile.h"
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <vector>
#include <cstdlib>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "local-dev"
#endif

static constexpr unsigned long WIFI_STA_RECONNECT_INTERVAL_MS = 30000;
static constexpr unsigned long WIFI_SOFTAP_RETRY_INTERVAL_MS = 300000; // 5 min

MailboxNetworkManager mailboxNetworkManager;

MailboxNetworkManager::MailboxNetworkManager() :
    _server(80),
    _softApMode(false),
    _lastWifiRetryMs(0)
{}

void MailboxNetworkManager::begin() {
    connectWiFi();
    setupWebServer();
    _server.begin();
    Serial.println("Web server started.");
}

void MailboxNetworkManager::connectWiFi() {
    Config config = configManager.getConfigCopy();
    _configuredSsid = config.ssid;
    _configuredPassword = config.password;
    bool connected = false;
    
    if (_configuredSsid.length() > 0) {
        Serial.print("Connecting to WiFi: ");
        Serial.println(_configuredSsid);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(_configuredSsid.c_str(), _configuredPassword.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startTime > 30000) {
                Serial.println("\nFailed to connect to WiFi within 30 seconds.");
                break;
            }
            delay(500);
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
            telemetryLog.logf("WIFI", "Connected to '%s' IP=%s RSSI=%d",
                              _configuredSsid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
            connected = true;
            _softApMode = false;
        }
    }

    if (!connected) {
        WiFi.mode(_configuredSsid.length() > 0 ? WIFI_AP_STA : WIFI_AP);
        WiFi.softAP("Paketkasten-Setup", SOFTAP_PASSWORD);
        telemetryLog.logf("WIFI", "SoftAP started IP=%s", WiFi.softAPIP().toString().c_str());
        _softApMode = true;
        _lastWifiRetryMs = millis();
    }
}

void MailboxNetworkManager::update() {
    if (_configuredSsid.length() == 0 || otaInProgress) {
        return;
    }
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return;
    }

    unsigned long now = millis();
    if (_softApMode) {
        if (WiFi.status() == WL_CONNECTED) {
            telemetryLog.logf("WIFI", "Recovered STA IP=%s, disabling SoftAP", WiFi.localIP().toString().c_str());
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            _softApMode = false;
            return;
        }
        // If no setup client is connected to the SoftAP, periodically retry the configured WiFi
        // (recovers automatically after power outages when the router boots slower than the ESP32).
        if (WiFi.softAPgetStationNum() == 0 && (now - _lastWifiRetryMs >= WIFI_SOFTAP_RETRY_INTERVAL_MS)) {
            _lastWifiRetryMs = now;
            telemetryLog.logf("WIFI", "SoftAP idle: retrying '%s'", _configuredSsid.c_str());
            WiFi.begin(_configuredSsid.c_str(), _configuredPassword.c_str());
        }
    } else if (WiFi.status() != WL_CONNECTED) {
        if (now - _lastWifiRetryMs >= WIFI_STA_RECONNECT_INTERVAL_MS) {
            _lastWifiRetryMs = now;
            telemetryLog.logf("WIFI", "Connection lost, attempting reconnect");
            WiFi.reconnect();
        }
    }
}

static void saveOrRemoveCert(const char* path, const String& content) {
    String trimmed = content;
    trimmed.trim();
    if (trimmed.length() == 0) {
        if (LittleFS.exists(path)) {
            LittleFS.remove(path);
            Serial.printf("Removed empty cert file: %s\n", path);
        }
        return;
    }
    if (!LittleFS.exists("/certs")) {
        LittleFS.mkdir("/certs");
    }
    File file = LittleFS.open(path, "w");
    if (file) {
        file.print(content);
        file.close();
        Serial.printf("Saved cert to LittleFS: %s\n", path);
    }
}

void MailboxNetworkManager::setupWebServer() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<h1>Paketkasten</h1><p>Filesystem missing. <a href='/update'>Upload littlefs.bin</a></p>");
        }
    });

    _server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/update.html")) {
            request->send(LittleFS, "/update.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<h1>OTA Update</h1>"
                "<form method='POST' action='/update?type=firmware' enctype='multipart/form-data'>"
                "Firmware: <input type='file' name='update'><input type='submit' value='Upload'></form>"
                "<form method='POST' action='/update?type=filesystem' enctype='multipart/form-data'>"
                "Filesystem: <input type='file' name='update'><input type='submit' value='Upload'></form>");
        }
    });

    _server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        bool ok = !Update.hasError() && Update.isFinished();
        otaInProgress = false;
        if (ok) {
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "OK");
            response->addHeader("Connection", "close");
            request->send(response);
            requestRestart(1000);
        } else {
            AsyncWebServerResponse *response = request->beginResponse(500, "text/plain", "FAIL");
            response->addHeader("Connection", "close");
            request->send(response);
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!index) {
            if (Update.isRunning()) {
                Update.abort();
            }
            otaInProgress = true;
            wiegandManager.detach();
            request->onDisconnect([]() {
                if (!shouldRestart) {
                    if (Update.isRunning()) {
                        Update.abort();
                        telemetryLog.logf("OTA", "Update aborted (client disconnected)");
                    }
                    otaInProgress = false;
                }
            });

            int cmd = U_FLASH; // Default to firmware update
            if (request->hasParam("type")) {
                String type = request->getParam("type")->value();
                if (type == "filesystem") {
                    cmd = U_SPIFFS;
                }
            }
            
            telemetryLog.logf("OTA", "Update start: %s (cmd=%d)", filename.c_str(), cmd);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
                Update.printError(Serial);
            }
        }
        if (!Update.hasError()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
        }
        if (final) {
            if (Update.end(true)) {
                telemetryLog.logf("OTA", "Update success: %uB", (unsigned)(index + len));
            } else {
                telemetryLog.logf("OTA", "Update failed (err=%u)", (unsigned)Update.getError());
                Update.printError(Serial);
            }
        }
    });

    _server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
        String jsonConfig;
        JsonDocument doc;
        Config config = configManager.getConfigCopy();
        doc["ssid"] = config.ssid;
        doc["mqttServer"] = config.mqttServer;
        doc["mqttPort"] = config.mqttPort;
        doc["mqttUser"] = config.mqttUser;
        doc["ownerCodes"] = config.ownerCodes;
        doc["deliveryCodes"] = config.deliveryCodes;
        doc["dutyCycleOpen"] = config.dutyCycleOpen;
        doc["dutyCycleClose"] = config.dutyCycleClose;
        doc["motorKickMs"] = config.motorKickMs;
        doc["selectedMelody"] = config.selectedMelody;
        doc["callbackUrl"] = config.callbackUrl;
        doc["autolock"] = config.autolock;
        doc["oneTimeCodes"] = config.oneTimeCodes;
        doc["oneTimeOpening"] = config.oneTimeOpening;
        doc["devMode"] = config.devMode;
        doc["mqttUseTls"] = config.mqttUseTls;
        doc["mqttSkipCertVal"] = config.mqttSkipCertVal;
        doc["callbackSkipCertVal"] = config.callbackSkipCertVal;

        // Load MQTT CA Cert from LittleFS
        String mqttCa = "";
        if (LittleFS.exists("/certs/mqtt_ca.pem")) {
            File file = LittleFS.open("/certs/mqtt_ca.pem", "r");
            if (file) {
                mqttCa = file.readString();
                file.close();
            }
        }
        doc["mqttCa"] = mqttCa;

        // Load Callback CA Cert from LittleFS
        String callbackCa = "";
        if (LittleFS.exists("/certs/callback_ca.pem")) {
            File file = LittleFS.open("/certs/callback_ca.pem", "r");
            if (file) {
                callbackCa = file.readString();
                file.close();
            }
        }
        doc["callbackCa"] = callbackCa;

        serializeJson(doc, jsonConfig);
        request->send(200, "application/json", jsonConfig);
    });

    _server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("Saving configuration...");

        configManager.updateConfig([request](Config& config) {
            config.ssid = request->arg("ssid");
            if (request->hasArg("password") && request->arg("password") != "") {
                config.password = request->arg("password");
            }
            if (request->hasArg("ownerCodes") && request->arg("ownerCodes").length() > 0) {
                config.ownerCodes = request->arg("ownerCodes");
            }
            if (request->hasArg("deliveryCodes") && request->arg("deliveryCodes").length() > 0) {
                config.deliveryCodes = request->arg("deliveryCodes");
            }
            if (request->hasArg("oneTimeCodes") && request->arg("oneTimeCodes").length() > 0) {
                config.oneTimeCodes = request->arg("oneTimeCodes");
            }
            config.mqttServer = request->arg("mqttServer");
            config.mqttUseTls = request->hasArg("mqttUseTls");
            int parsedPort = request->arg("mqttPort").toInt();
            if (parsedPort > 0 && parsedPort <= 65535) {
                config.mqttPort = parsedPort;
            } else {
                config.mqttPort = config.mqttUseTls ? 8883 : 1883;
            }
            config.mqttUser = request->arg("mqttUser");
            if (request->hasArg("mqttPassword") && request->arg("mqttPassword") != "") {
                config.mqttPassword = request->arg("mqttPassword");
            }
            config.dutyCycleOpen = constrain(request->arg("dutyCycleOpen").toInt(), MotorProfile::OPEN_DUTY_MIN, MotorProfile::OPEN_DUTY_MAX);
            config.dutyCycleClose = constrain(request->arg("dutyCycleClose").toInt(), MotorProfile::CLOSE_DUTY_MIN, MotorProfile::CLOSE_DUTY_MAX);
            if (request->hasArg("motorKickMs")) {
                config.motorKickMs = constrain(request->arg("motorKickMs").toInt(), MotorProfile::KICK_MS_MIN, MotorProfile::KICK_MS_MAX);
            }
            config.selectedMelody = request->arg("selectedMelody");
            config.callbackUrl = request->arg("callbackUrl");
            config.autolock = request->hasArg("autolock");
            config.oneTimeOpening = request->hasArg("oneTimeOpening");
            config.devMode = request->hasArg("devMode");
            config.mqttSkipCertVal = request->hasArg("mqttSkipCertVal");
            config.callbackSkipCertVal = request->hasArg("callbackSkipCertVal");
        }, true);

        if (request->hasArg("mqttCa")) {
            saveOrRemoveCert("/certs/mqtt_ca.pem", request->arg("mqttCa"));
        }
        if (request->hasArg("callbackCa")) {
            saveOrRemoveCert("/certs/callback_ca.pem", request->arg("callbackCa"));
        }

        telemetryLog.logf("CONFIG", "Configuration saved; scheduling restart");
        // Never call delay() inside an AsyncWebServer handler; let appTask restart cleanly after sending response.
        request->send(200, "text/plain", "OK");
        requestRestart(1000);
    });

    _server.on("/factoryreset", HTTP_POST, [](AsyncWebServerRequest *request){
        telemetryLog.logf("CONFIG", "Factory reset requested");
        configManager.factoryReset();
        request->send(200, "text/plain", "OK");
        requestRestart(1000);
    });

    _server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request){
        int16_t status = WiFi.scanComplete();
        bool scanRunning = (status == WIFI_SCAN_RUNNING);
        static String cachedWifiJson = "[]";

        if (status >= 0) {
            JsonDocument wifiDoc;
            JsonArray networks = wifiDoc.to<JsonArray>();
            for (int i = 0; i < status; ++i) {
                JsonObject net = networks.add<JsonObject>();
                net["ssid"] = WiFi.SSID(i);
                net["rssi"] = WiFi.RSSI(i);
                net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            }
            cachedWifiJson = "";
            serializeJson(wifiDoc, cachedWifiJson);
            WiFi.scanDelete();
        }

        const MotorSettings settings = configManager.getMotorSettings();
        const WiegandFrameStats wStats = wiegandManager.getLastFrameStats();
        String jsonResponse;
        JsonDocument doc;
        doc["wiegand_id"] = lastScannedWiegandId;
        doc["last_code"] = lastKeypadCode;
        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["mailbox_state"] = getMailboxStateString();
        doc["closed_switch"] = switchManager.isClosedPressed();
        doc["parcel_switch"] = switchManager.isParcelPressed();
        doc["mail_switch"] = switchManager.isMailPressed();
        doc["delivery_blocked"] = deliveryBlocked;
        doc["one_time_opening"] = settings.oneTimeOpening;
        doc["one_time_codes"] = configManager.getOneTimeCodesCopy();
        doc["dev_mode"] = settings.devMode;
        doc["free_heap"] = (uint32_t)ESP.getFreeHeap();
        doc["uptime_ms"] = (unsigned long)millis();
        doc["mqtt_connected"] = mqttManager.isConnected();
        doc["wiegand_attached"] = wiegandManager.isAttached();
        doc["wiegand_bits"] = wStats.bitCount;
        doc["wiegand_min_gap_us"] = wStats.minBitGapUs;
        doc["wiegand_max_gap_us"] = wStats.maxBitGapUs;
        doc["wiegand_duration_ms"] = wStats.durationMs;
        doc["wiegand_rejected"] = wStats.rejectedPulses;
        doc["wiegand_glitches"] = wiegandManager.getSingleBitGlitchCount();
        doc["wifi_scan_running"] = scanRunning;
        doc["wifi_rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
        doc["last_stroke_ms"] = (unsigned long)lastStrokeMs;
        doc["last_stroke_from"] = mailboxStateName(lastStrokeFrom);
        doc["last_stroke_to"] = mailboxStateName(lastStrokeTo);
        doc["last_stroke_escalated"] = (bool)lastStrokeEscalated;

        if (cachedWifiJson != "") {
            doc["wifi_networks"] = serialized(cachedWifiJson);
        } else {
            doc["wifi_networks"] = serialized("[]");
        }

        serializeJson(doc, jsonResponse);
        request->send(200, "application/json", jsonResponse);
    });

    _server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request){
        uint32_t sinceSeq = 0;
        if (request->hasParam("since")) {
            sinceSeq = strtoul(request->getParam("since")->value().c_str(), nullptr, 10);
        }
        // Heap-allocate snapshot buffer to avoid blowing async_tcp's small stack (~5.8KB).
        std::vector<TelemetryEntry> entries(TelemetryLog::CAPACITY);
        uint32_t latestSeq = 0;
        size_t count = telemetryLog.copySince(sinceSeq, entries.data(), entries.size(), &latestSeq);

        JsonDocument doc;
        doc["latest_seq"] = latestSeq;
        JsonArray arr = doc["entries"].to<JsonArray>();
        for (size_t i = 0; i < count; ++i) {
            JsonObject obj = arr.add<JsonObject>();
            obj["seq"] = entries[i].seq;
            obj["ms"] = entries[i].uptimeMs;
            obj["cat"] = entries[i].category;
            obj["msg"] = entries[i].message;
        }
        String jsonResponse;
        serializeJson(doc, jsonResponse);
        request->send(200, "application/json", jsonResponse);
    });

    _server.on("/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request){
        telemetryLog.clear();
        request->send(200, "text/plain", "OK");
    });

    _server.on("/dev-mode", HTTP_POST, [](AsyncWebServerRequest *request){
        String val;
        if (request->hasParam("enabled", true)) {
            val = request->getParam("enabled", true)->value();
        } else if (request->hasParam("enabled")) {
            val = request->getParam("enabled")->value();
        } else {
            request->send(400, "text/plain", "Bad Request");
            return;
        }
        val.toLowerCase();
        bool enabled = (val == "1" || val == "true" || val == "on");
        configManager.setDevMode(enabled);
        telemetryLog.logf("SYSTEM", "Dev telemetry mode %s", enabled ? "enabled" : "disabled");
        request->send(200, "text/plain", "OK");
    });

    _server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING) {
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else {
            if (status >= 0) {
                WiFi.scanDelete();
            }
            WiFi.scanNetworks(true, false);
            request->send(200, "application/json", "{\"status\":\"started\"}");
        }
    });

    _server.on("/save-onetime-codes", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("oneTimeCodes", true)) {
            configManager.setOneTimeCodes(request->getParam("oneTimeCodes", true)->value());
            Serial.println("One-time codes saved dynamically.");
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Bad Request");
        }
    });

    _server.on("/playMelody", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("melody", true)) {
            String melodyType = request->getParam("melody", true)->value();
            Serial.print("Playing melody: ");
            Serial.println(melodyType);
            melodyPlayer.play(melodyType);
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Bad Request");
        }
    });

    _server.on("/open", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("type", true)) {
            String type = request->getParam("type", true)->value();
            bool accepted = false;
            if (type == "parcel") {
                accepted = requestParcelOpening("webinterface");
            } else if (type == "all") {
                accepted = requestMailOpening("webinterface");
            } else {
                request->send(400, "text/plain", "Invalid type");
                return;
            }
            if (accepted) {
                request->send(200, "text/plain", "OK");
            } else {
                String reason = String("Cannot open in state ") + mailboxStateName(currentState) + " (or cooldown active)";
                request->send(409, "text/plain", reason);
            }
        } else {
            request->send(400, "text/plain", "Bad Request");
        }
    });

    _server.on("/lock", HTTP_POST, [](AsyncWebServerRequest *request){
        if (requestLocking("webinterface")) {
            request->send(200, "text/plain", "OK");
        } else {
            request->send(409, "text/plain", "Cannot lock right now (motor busy or OTA active)");
        }
    });

    _server.on("/reset-error", HTTP_POST, [](AsyncWebServerRequest *request){
        if (resetMotorError("webinterface")) {
            request->send(200, "text/plain", "OK");
        } else {
            request->send(409, "text/plain", "Cannot reset error while motor is moving or OTA active");
        }
    });
}
