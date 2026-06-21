#include "MqttManager.h"
#include "ConfigManager.h"
#include "state.h"
#include <LittleFS.h>

MqttManager mqttManager;

MqttManager::MqttManager() :
    _netClient(nullptr)
{}

MqttManager::~MqttManager() {
    if (_netClient != nullptr) {
        delete _netClient;
    }
}

void MqttManager::begin(void (*callback)(char* topic, byte* payload, unsigned int length)) {
    Config& config = configManager.getConfig();
    if (config.mqttServer != "") {
        Serial.print("Setting up MQTT server: ");
        Serial.println(config.mqttServer);

        if (_netClient != nullptr) {
            delete _netClient;
            _netClient = nullptr;
        }

        if (config.mqttUseTls) {
            Serial.println("MQTT: Using TLS (Secure connection)");
            WiFiClientSecure* secureClient = new WiFiClientSecure();
            if (config.mqttSkipCertVal) {
                Serial.println("MQTT: Skipping certificate validation (Insecure)");
                secureClient->setInsecure();
            } else {
                String caCert = "";
                if (LittleFS.exists("/certs/mqtt_ca.pem")) {
                    File file = LittleFS.open("/certs/mqtt_ca.pem", "r");
                    if (file) {
                        caCert = file.readString();
                        file.close();
                    }
                }
                if (caCert.length() > 0) {
                    Serial.println("MQTT: Setting Root CA certificate");
                    secureClient->setCACert(caCert.c_str());
                } else {
                    Serial.println("MQTT Warning: TLS requested but no CA certificate found. Falling back to insecure mode.");
                    secureClient->setInsecure();
                }
            }
            _netClient = secureClient;
        } else {
            Serial.println("MQTT: Using standard TCP (Unencrypted connection)");
            _netClient = new WiFiClient();
        }

        _mqttClient.setClient(*_netClient);
        _mqttClient.setServer(config.mqttServer.c_str(), config.mqttPort);
        _mqttClient.setCallback(callback);
    }
}

bool MqttManager::isConnected() {
    return _mqttClient.connected();
}

void MqttManager::publish(const char* topic, const char* payload) {
    _mqttClient.publish(topic, payload);
}

void MqttManager::reconnect() {
    Config& config = configManager.getConfig();
    long now = millis();
    static long lastReconnectAttempt = 0;
    if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        Serial.println("Attempting MQTT connection...");
        if (_mqttClient.connect("Paketkasten", config.mqttUser.c_str(), config.mqttPassword.c_str())) {
            Serial.println("MQTT connected.");
            _mqttClient.subscribe("paketkasten/command");
            publishState();
        } else {
            Serial.print("MQTT connection failed, rc=");
            Serial.println(_mqttClient.state());
        }
    }
}

void MqttManager::handleQueue() {
    if (_mqttClient.connected() && xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (!mqttMessageQueue.empty()) {
            std::vector<String> localQueue = mqttMessageQueue;
            mqttMessageQueue.clear();
            xSemaphoreGive(mqttQueueMutex);

            String stateTopic = "paketkasten/state";
            for (const auto& msg : localQueue) {
                Serial.print("Publishing state to MQTT: ");
                Serial.println(msg);
                _mqttClient.publish(stateTopic.c_str(), msg.c_str());
            }
        } else {
            xSemaphoreGive(mqttQueueMutex);
        }
    }
}

void MqttManager::update() {
    Config& config = configManager.getConfig();
    if (config.mqttServer != "" && _netClient != nullptr) {
        _mqttClient.loop();
        if (!_mqttClient.connected()) {
            reconnect();
        }
        handleQueue();
    }
}
