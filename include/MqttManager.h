#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

class MqttManager {
public:
    MqttManager();
    ~MqttManager();
    void begin(void (*callback)(char* topic, byte* payload, unsigned int length));
    void update();
    bool isConnected();
    void publish(const char* topic, const char* payload);

private:
    void handleQueue();
    void reconnect();

    Client* _netClient = nullptr;
    PubSubClient _mqttClient;
    String _caCert; // Must outlive WiFiClientSecure because setCACert() stores a raw pointer
    String _mqttServer;
    String _mqttUser;
    String _mqttPassword;
    unsigned long _lastReconnectAttempt = 0;
    volatile bool _connected = false;
};

extern MqttManager mqttManager;

#endif
