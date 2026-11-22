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
  int motorKickMs;   // breakaway kick duration for opening (halved automatically when locking)
  String selectedMelody;
  String callbackUrl;
  bool autolock;
  String oneTimeCodes;
  bool oneTimeOpening;
  bool mqttUseTls;
  bool mqttSkipCertVal;
  bool callbackSkipCertVal;
  bool devMode;      // enables live buffered telemetry panel in WebUI
  bool hasMailCompartment; // true = mailbox has separate mail slot (Brieffach); false = parcel only
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
const char* const MOTOR_KICK_MS_KEY = "motorKickMs";
const char* const SELECTED_MELODY_KEY = "selectedMelody";
const char* const CALLBACK_URL_KEY = "callbackUrl";
const char* const AUTOLOCK_KEY = "autolock";
const char* const ONE_TIME_CODES_KEY = "oneTimeCodes";
const char* const ONE_TIME_OPENING_KEY = "oneTimeOpen";
const char* const DELIVERY_BLOCKED_KEY = "delBlocked";
const char* const MQTT_USE_TLS_KEY = "mqttUseTls";
const char* const MQTT_SKIP_CERT_VAL_KEY = "mqttSkipCert";
const char* const CALLBACK_SKIP_CERT_VAL_KEY = "cbSkipCert";
const char* const DEV_MODE_KEY = "devMode";
const char* const HAS_MAIL_KEY = "hasMail";

#endif
