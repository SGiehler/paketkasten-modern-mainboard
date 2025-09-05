#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

struct Config {
  String ssid;
  String password;
  String ownerCodes;
  String deliveryCodes;
  String mqttServer;
  int mqttPort;
  String mqttUser;
  String mqttPassword;
  int dutyCycleOpen;
  int dutyCycleClose;
  String selectedMelody;
};

const char* const PREFERENCES_NAMESPACE = "mailbox";
const char* const SSID_KEY = "ssid";
const char* const PASSWORD_KEY = "password";
const char* const OWNER_CODE_KEY = "ownerCode";
const char* const DELIVERY_CODE_KEY = "deliveryCode";
const char* const MQTT_SERVER_KEY = "mqttServer";
const char* const MQTT_PORT_KEY = "mqttPort";
const char* const MQTT_USER_KEY = "mqttUser";
const char* const MQTT_PASSWORD_KEY = "mqttPassword";
const char* const DUTY_CYCLE_OPEN_KEY = "dutyCycleOpen";
const char* const DUTY_CYCLE_CLOSE_KEY = "dutyCycleClose";
const char* const SELECTED_MELODY_KEY = "selectedMelody";

#endif
