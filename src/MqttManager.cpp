#include "MqttManager.h"
#include "ConfigManager.h"
#include "TelemetryLog.h"
#include "state.h"
#include <LittleFS.h>

MqttManager mqttManager;

MqttManager::MqttManager() :
    _netClient(nullptr),
    _lastReconnectAttempt(0),
    _connected(false)
{}

MqttManager::~MqttManager() {
    if (_netClient != nullptr) {
        delete _netClient;
        _netClient = nullptr;
    }
}

void MqttManager::begin(void (*callback)(char* topic, byte* payload, unsigned int length)) {
    Config config = configManager.getConfigCopy();
    _mqttServer = config.mqttServer;
    _mqttUser = config.mqttUser;
    _mqttPassword = config.mqttPassword;
    _caCert = "";
    _connected = false;

    if (_mqttServer.length() > 0) {
        if (_netClient != nullptr) {
            delete _netClient;
            _netClient = nullptr;
        }

        if (config.mqttUseTls) {
            WiFiClientSecure* secureClient = new WiFiClientSecure();
            if (config.mqttSkipCertVal) {
                telemetryLog.logf("MQTT", "Setup %s:%d (TLS, skip cert verify)", _mqttServer.c_str(), config.mqttPort);
                secureClient->setInsecure();
            } else {
                if (LittleFS.exists("/certs/mqtt_ca.pem")) {
                    File file = LittleFS.open("/certs/mqtt_ca.pem", "r");
                    if (file) {
                        _caCert = file.readString();
                        file.close();
                    }
                }
                if (_caCert.length() > 0) {
                    telemetryLog.logf("MQTT", "Setup %s:%d (TLS + Root CA)", _mqttServer.c_str(), config.mqttPort);
                    // _caCert is a class member so its buffer stays valid for the lifetime of secureClient
                    secureClient->setCACert(_caCert.c_str());
                } else {
                    telemetryLog.logf("MQTT", "Setup %s:%d (TLS without CA -> insecure fallback)", _mqttServer.c_str(), config.mqttPort);
                    secureClient->setInsecure();
                }
            }
            _netClient = secureClient;
        } else {
            telemetryLog.logf("MQTT", "Setup %s:%d (TCP)", _mqttServer.c_str(), config.mqttPort);
            _netClient = new WiFiClient();
        }

        _mqttClient.setClient(*_netClient);
        _mqttClient.setServer(_mqttServer.c_str(), config.mqttPort);
        _mqttClient.setCallback(callback);
    }
}

bool MqttManager::isConnected() {
    return _connected;
}

void MqttManager::publish(const char* topic, const char* payload) {
    _mqttClient.publish(topic, payload);
}

void MqttManager::reconnect() {
    if (WiFi.status() != WL_CONNECTED) {
        _connected = false;
        return;
    }
    unsigned long now = millis();
    if (_lastReconnectAttempt == 0 || (now - _lastReconnectAttempt > 5000)) {
        _lastReconnectAttempt = now;
        bool connected = false;
        if (_mqttUser.length() > 0) {
            connected = _mqttClient.connect("Paketkasten", _mqttUser.c_str(), _mqttPassword.c_str());
        } else {
            connected = _mqttClient.connect("Paketkasten");
        }
        _connected = connected;
        if (connected) {
            telemetryLog.logf("MQTT", "Connected to %s", _mqttServer.c_str());
            _mqttClient.subscribe("paketkasten/command");
            publishState();
        } else {
            telemetryLog.logf("MQTT", "Connect failed (rc=%d)", _mqttClient.state());
        }
    }
}

void MqttManager::handleQueue() {
    if (_connected && xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (!mqttMessageQueue.empty()) {
            std::vector<String> localQueue;
            localQueue.swap(mqttMessageQueue);
            xSemaphoreGive(mqttQueueMutex);

            const char* stateTopic = "paketkasten/state";
            for (const auto& msg : localQueue) {
                Serial.print("Publishing state to MQTT: ");
                Serial.println(msg);
                _mqttClient.publish(stateTopic, msg.c_str());
            }
        } else {
            xSemaphoreGive(mqttQueueMutex);
        }
    }
}

void MqttManager::update() {
    if (_mqttServer.length() > 0 && _netClient != nullptr) {
        if (WiFi.status() != WL_CONNECTED) {
            _connected = false;
            return;
        }
        _mqttClient.loop();
        bool nowConnected = _mqttClient.connected();
        if (_connected && !nowConnected) {
            telemetryLog.logf("MQTT", "Disconnected (rc=%d)", _mqttClient.state());
        }
        _connected = nowConnected;
        if (!nowConnected) {
            reconnect();
        }
        handleQueue();
    } else {
        _connected = false;
    }
}
