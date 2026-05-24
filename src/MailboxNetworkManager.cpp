#include "MailboxNetworkManager.h"
#include "ConfigManager.h"
#include "MelodyPlayer.h"
#include "SwitchManager.h"
#include "state.h"
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "local-dev"
#endif

MailboxNetworkManager mailboxNetworkManager;

MailboxNetworkManager::MailboxNetworkManager() :
    _server(80)
{}

void MailboxNetworkManager::begin() {
    connectWiFi();
    setupWebServer();
    _server.begin();
    Serial.println("Web server started.");
}

void MailboxNetworkManager::connectWiFi() {
    Config& config = configManager.getConfig();
    bool connected = false;
    
    if (config.ssid != "") {
        Serial.print("Connecting to WiFi: ");
        Serial.println(config.ssid);
        WiFi.begin(config.ssid.c_str(), config.password.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startTime > 30000) {
                Serial.println("Failed to connect to WiFi within 30 seconds.");
                break;
            }
            delay(500);
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Connected to WiFi");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            connected = true;
        }
    }

    if (!connected) {
        Serial.println("Starting SoftAP for configuration.");
        WiFi.softAP("Paketkasten-Setup", SOFTAP_PASSWORD);
        Serial.print("SoftAP IP address: ");
        Serial.println(WiFi.softAPIP());
    }
}

void MailboxNetworkManager::setupWebServer() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/index.html", "text/html");
    });

    _server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/update.html", "text/html");
    });

    _server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        shouldRestart = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldRestart ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if(!index){
            int cmd = U_FLASH; // Default to firmware update
            if(request->hasParam("type")) {
                String type = request->getParam("type")->value();
                if(type == "filesystem") {
                    cmd = U_SPIFFS;
                }
            }
            
            Serial.printf("Update Start: %s\n", filename.c_str());
            if(!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)){
                Update.printError(Serial);
            }
        }
        if(!Update.hasError()){
            if(Update.write(data, len) != len){
                Update.printError(Serial);
            }
        }
        if(final){
            if(Update.end(true)){
                Serial.printf("Update Success: %uB\n", index+len);
            } else {
                Update.printError(Serial);
            }
        }
    });

    _server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
        String jsonConfig;
        JsonDocument doc;
        Config& config = configManager.getConfig();
        doc["ssid"] = config.ssid;
        doc["mqttServer"] = config.mqttServer;
        doc["mqttPort"] = config.mqttPort;
        doc["mqttUser"] = config.mqttUser;
        doc["ownerCodes"] = config.ownerCodes;
        doc["deliveryCodes"] = config.deliveryCodes;
        doc["dutyCycleOpen"] = config.dutyCycleOpen;
        doc["dutyCycleClose"] = config.dutyCycleClose;
        doc["selectedMelody"] = config.selectedMelody;
        doc["callbackUrl"] = config.callbackUrl;
        doc["autolock"] = config.autolock;
        doc["oneTimeCodes"] = config.oneTimeCodes;
        doc["oneTimeOpening"] = config.oneTimeOpening;
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
        Config& config = configManager.getConfig();
        config.ssid = request->arg("ssid");
        if (request->hasArg("password") && request->arg("password") != "") {
            config.password = request->arg("password");
        }
        config.ownerCodes = request->arg("ownerCodes");
        config.deliveryCodes = request->arg("deliveryCodes");
        config.mqttServer = request->arg("mqttServer");
        config.mqttPort = request->arg("mqttPort").toInt();
        config.mqttUser = request->arg("mqttUser");
        if (request->hasArg("mqttPassword") && request->arg("mqttPassword") != "") {
            config.mqttPassword = request->arg("mqttPassword");
        }
        config.dutyCycleOpen = request->arg("dutyCycleOpen").toInt();
        config.dutyCycleClose = request->arg("dutyCycleClose").toInt();
        config.selectedMelody = request->arg("selectedMelody");
        config.callbackUrl = request->arg("callbackUrl");
        config.autolock = request->hasArg("autolock");
        config.oneTimeOpening = request->hasArg("oneTimeOpening");
        config.mqttUseTls = request->hasArg("mqttUseTls");
        config.mqttSkipCertVal = request->hasArg("mqttSkipCertVal");
        config.callbackSkipCertVal = request->hasArg("callbackSkipCertVal");

        // Save MQTT CA Cert to LittleFS
        if (request->hasArg("mqttCa")) {
            if (!LittleFS.exists("/certs")) {
                LittleFS.mkdir("/certs");
            }
            File file = LittleFS.open("/certs/mqtt_ca.pem", "w");
            if (file) {
                file.print(request->arg("mqttCa"));
                file.close();
                Serial.println("MQTT CA cert saved to LittleFS.");
            }
        }

        // Save Callback CA Cert to LittleFS
        if (request->hasArg("callbackCa")) {
            if (!LittleFS.exists("/certs")) {
                LittleFS.mkdir("/certs");
            }
            File file = LittleFS.open("/certs/callback_ca.pem", "w");
            if (file) {
                file.print(request->arg("callbackCa"));
                file.close();
                Serial.println("Callback CA cert saved to LittleFS.");
            }
        }

        configManager.save();
        delay(500);
        Serial.println("Configuration saved. Restarting...");
        request->send(200, "text/plain", "OK");
        delay(2000);
        shouldRestart = true;
    });

    _server.on("/factoryreset", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("Factory reset requested.");
        configManager.factoryReset();
        request->send(200, "text/plain", "OK");
        delay(2000);
        shouldRestart = true;
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
        doc["one_time_opening"] = configManager.getConfig().oneTimeOpening;
        doc["wifi_scan_running"] = scanRunning;

        if (cachedWifiJson != "") {
            doc["wifi_networks"] = serialized(cachedWifiJson);
        } else {
            doc["wifi_networks"] = serialized("[]");
        }

        serializeJson(doc, jsonResponse);
        request->send(200, "application/json", jsonResponse);
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
            Config& config = configManager.getConfig();
            config.oneTimeCodes = request->getParam("oneTimeCodes", true)->value();
            configManager.save();
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
            if (type == "parcel") {
                requestParcelOpening("webinterface");
            } else if (type == "all") {
                requestMailOpening("webinterface");
            }
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "Bad Request");
        }
    });
}
