#include <Arduino.h>
#include "state.h"
#include "config.h"
#include <Wiegand.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Pin definitions
const int MOTOR_PIN_1 = 33;
const int MOTOR_PIN_2 = 25;
const int GREEN_LED_PIN = 19;
const int RED_LED_PIN = 18;
const int CLOSED_SWITCH_PIN = 17;
const int PARCEL_SWITCH_PIN = 16;
const int MAIL_SWITCH_PIN = 4;
const int WIEGAND_D0_PIN = 27;
const int WIEGAND_D1_PIN = 26;

// Inverting flags
const bool INVERT_MOTOR_DIRECTION = false;
const bool INVERT_SWITCH_STATE = false;
const char* SOFTAP_PASSWORD = "tuVlZfEr5^p!fcUH0QWU";

// Movement Settings
const int PWM_FREQ = 50000;
const int FULL_POWER_MS = 5;
const int RAMP_DOWN_MS = 15;

MailboxState currentState = LOCKED;
Wiegand wiegand;
AsyncWebServer server(80);
Preferences preferences;
Config config;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
char lastScannedWiegandId[20] = "";
unsigned long openStateEnterTime = 0;
unsigned long motorStartTime = 0;

// Function declarations
void setupMotor();
void setupLeds();
void setupSwitches();
void setupWiegand();
void setupWebServer();
void setupMqtt();

void loopMotor();
void loopLeds();
void loopSwitches();
void loopWiegand();
void loopWebServer();
void loopMqtt();

String getMailboxStateString();
void receivedWiegandCode(char* code, uint8_t bits);
void loadConfiguration();
void saveConfiguration();
void publishState();
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  loadConfiguration();
  Serial.println("Configuration loaded.");
  setupMotor();
  Serial.println("Motor setup complete.");
  setupLeds();
  Serial.println("LEDs setup complete.");
  setupSwitches();
  Serial.println("Switches setup complete.");
  setupWiegand();
  Serial.println("Wiegand setup complete.");
  setupWebServer();
  Serial.println("Web server setup complete.");
  setupMqtt();
  Serial.println("MQTT setup complete.");
  Serial.println("Setup complete.");
  Serial.println("Entering loop...");
}

void loop() {

  static MailboxState lastLoggedState = (MailboxState)-1;
  if (currentState != lastLoggedState) {
    Serial.print("Current state: ");
    Serial.println(getMailboxStateString());
    lastLoggedState = currentState;
  }

  if ((currentState == PARCEL_OPEN || currentState == MAIL_OPEN) && (millis() - openStateEnterTime > 1000)) {
    currentState = LOCKING;
  }

  loopMotor();
  delay(5);
  loopLeds();
  loopSwitches();
  loopWiegand();
  loopWebServer();
  loopMqtt();
}

void loadConfiguration() {
  preferences.begin(PREFERENCES_NAMESPACE, false);
  config.ssid = preferences.getString(SSID_KEY, "");
  config.password = preferences.getString(PASSWORD_KEY, "");
  config.ownerCodes = preferences.getString(OWNER_CODE_KEY, "[]");
  config.deliveryCodes = preferences.getString(DELIVERY_CODE_KEY, "[]");
  config.mqttServer = preferences.getString(MQTT_SERVER_KEY, "");
  config.mqttPort = preferences.getInt(MQTT_PORT_KEY, 1883);
  config.mqttUser = preferences.getString(MQTT_USER_KEY, "");
  config.mqttPassword = preferences.getString(MQTT_PASSWORD_KEY, "");
  config.dutyCycleOpen = preferences.getInt(DUTY_CYCLE_OPEN_KEY, 150);
  config.dutyCycleClose = preferences.getInt(DUTY_CYCLE_CLOSE_KEY, 120);
  preferences.end();
}

void saveConfiguration() {
  preferences.begin(PREFERENCES_NAMESPACE, false);
  preferences.putString(SSID_KEY, config.ssid);
  preferences.putString(PASSWORD_KEY, config.password);
  preferences.putString(OWNER_CODE_KEY, config.ownerCodes);
  preferences.putString(DELIVERY_CODE_KEY, config.deliveryCodes);
  preferences.putString(MQTT_SERVER_KEY, config.mqttServer);
  preferences.putInt(MQTT_PORT_KEY, config.mqttPort);
  preferences.putString(MQTT_USER_KEY, config.mqttUser);
  preferences.putString(MQTT_PASSWORD_KEY, config.mqttPassword);
  preferences.putInt(DUTY_CYCLE_OPEN_KEY, config.dutyCycleOpen);
  preferences.putInt(DUTY_CYCLE_CLOSE_KEY, config.dutyCycleClose);
  preferences.end();
}

void setupMotor() {
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  analogWriteFrequency(MOTOR_PIN_1, PWM_FREQ);
  analogWriteFrequency(MOTOR_PIN_2, PWM_FREQ);
  analogWrite(MOTOR_PIN_1, 0);
  analogWrite(MOTOR_PIN_2, 0);
}

void setupLeds() {
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);
}

void setupSwitches() {
  pinMode(CLOSED_SWITCH_PIN, INPUT);
  pinMode(PARCEL_SWITCH_PIN, INPUT);
  pinMode(MAIL_SWITCH_PIN, INPUT);
}

void setupWiegand() {
  wiegand.begin(WIEGAND_D0_PIN, WIEGAND_D1_PIN);
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    String jsonConfig;
    JsonDocument doc;
    doc["ssid"] = config.ssid;
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["ownerCodes"] = config.ownerCodes;
    doc["deliveryCodes"] = config.deliveryCodes;
    doc["dutyCycleOpen"] = config.dutyCycleOpen;
    doc["dutyCycleClose"] = config.dutyCycleClose;
    serializeJson(doc, jsonConfig);
    request->send(200, "application/json", jsonConfig);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Saving configuration...");
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
    saveConfiguration();
    Serial.println("Configuration saved. Restarting...");
    request->send(200, "text/plain", "Configuration saved. Please restart the device.");
    delay(1000);
    ESP.restart();
  });

  server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request){
    String jsonResponse;
    JsonDocument doc;
    doc["wiegand_id"] = lastScannedWiegandId;
    doc["mailbox_state"] = getMailboxStateString();
    doc["closed_switch"] = digitalRead(CLOSED_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
    doc["parcel_switch"] = digitalRead(PARCEL_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
    doc["mail_switch"] = digitalRead(MAIL_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  server.on("/open", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("type", true)) {
      String type = request->getParam("type", true)->value();
      if (type == "parcel") {
        if (currentState == LOCKED) {
          Serial.println("Web command: OPEN_PARCEL. State -> OPENING_TO_PARCEL");
          currentState = OPENING_TO_PARCEL;
        }
      } else if (type == "all") {
        if (currentState == LOCKED) {
          Serial.println("Web command: OPEN_ALL. State -> OPENING_TO_MAIL");
          currentState = OPENING_TO_MAIL;
        }
      }
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

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
        Serial.println("\nConnected to WiFi");
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

  server.begin();
  Serial.println("Web server started.");
}

void setupMqtt() {
  if (config.mqttServer != "") {
    Serial.print("Setting up MQTT server: ");
    Serial.println(config.mqttServer);
    mqttClient.setServer(config.mqttServer.c_str(), config.mqttPort);
    mqttClient.setCallback(mqttCallback);
  }
}

void loopMotor() {
  static MailboxState lastMotorState = (MailboxState)-1; // To detect state changes

  if ((currentState == OPENING_TO_PARCEL || currentState == OPENING_TO_MAIL || currentState == LOCKING) && lastMotorState != currentState) {
      motorStartTime = millis();
  }
  lastMotorState = currentState;

  if ((currentState == OPENING_TO_PARCEL || currentState == OPENING_TO_MAIL || currentState == LOCKING) && (millis() - motorStartTime > 2000)) {
      currentState = MOTOR_ERROR;
  }

  unsigned long elapsedTime = millis() - motorStartTime;
  int dutyCycle;

  switch (currentState) {
    case OPENING_TO_PARCEL:
    case OPENING_TO_MAIL:
      dutyCycle = config.dutyCycleOpen;
      if (elapsedTime < FULL_POWER_MS) {
        dutyCycle = 255;
      } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) {
        dutyCycle = 255 - (255 - config.dutyCycleOpen) * (elapsedTime - 100) / RAMP_DOWN_MS;
      }
      if (INVERT_MOTOR_DIRECTION) {
        analogWrite(MOTOR_PIN_1, 0);
        analogWrite(MOTOR_PIN_2, dutyCycle);
      } else {
        analogWrite(MOTOR_PIN_1, dutyCycle);
        analogWrite(MOTOR_PIN_2, 0);
      }
      break;
    case LOCKING:
      dutyCycle = config.dutyCycleClose;
      if (elapsedTime < FULL_POWER_MS) {
        dutyCycle = 255;
      } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) { 
        dutyCycle = 255 - (255 - config.dutyCycleClose) * (elapsedTime - 50) / RAMP_DOWN_MS;
      }
      if (INVERT_MOTOR_DIRECTION) {
        analogWrite(MOTOR_PIN_1, dutyCycle);
        analogWrite(MOTOR_PIN_2, 0);
      } else {
        analogWrite(MOTOR_PIN_1, 0);
        analogWrite(MOTOR_PIN_2, dutyCycle);
      }
      break;
    case LOCKED:
    case PARCEL_OPEN:
    case MAIL_OPEN:
    case MOTOR_ERROR:
      analogWrite(MOTOR_PIN_1, 0);
      analogWrite(MOTOR_PIN_2, 0);
      break;
  }
}

void loopLeds() {
  switch (currentState) {
    case LOCKED:
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, LOW);
      break;
    case PARCEL_OPEN:
    case MAIL_OPEN:
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(RED_LED_PIN, LOW);
      break;
    case OPENING_TO_PARCEL:
    case OPENING_TO_MAIL:
    case LOCKING:
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, HIGH);
      break;
    case MOTOR_ERROR:
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(RED_LED_PIN, HIGH);
      break;
  }
}

void loopSwitches() {
  bool closedSwitch = digitalRead(CLOSED_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
  bool parcelSwitch = digitalRead(PARCEL_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
  bool mailSwitch = digitalRead(MAIL_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
  static MailboxState lastState = currentState;

  switch (currentState) {
    case OPENING_TO_PARCEL:
      if (parcelSwitch) {
        currentState = PARCEL_OPEN;
        openStateEnterTime = millis();
      }
      break;
    case OPENING_TO_MAIL:
      if (mailSwitch) {
        currentState = MAIL_OPEN;
        openStateEnterTime = millis();
      }
      break;
    case LOCKING:
      if (closedSwitch) {
        currentState = LOCKED;
      }
      break;
    default:
      break;
  }

  if (currentState != lastState) {
    publishState();
    lastState = currentState;
  }
}

void loopWiegand() {
  if (currentState != LOCKED) {
    return;
  }
  wiegand.loop();
  if (wiegand.isAvailable()) {
    char codeStr[12];
    snprintf(codeStr, sizeof(codeStr), "%lX", wiegand.getCode());
    Serial.print("Wiegand code received: ");
    Serial.println(codeStr);
    strncpy(lastScannedWiegandId, codeStr, sizeof(lastScannedWiegandId) - 1);
    lastScannedWiegandId[sizeof(lastScannedWiegandId) - 1] = '\0';

    if (currentState == LOCKED) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, config.ownerCodes);

      if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
          if (String(obj["code"]) == String(codeStr)) {
            Serial.println("Owner code matched. State -> OPENING_TO_MAIL");
            currentState = OPENING_TO_MAIL;
            return; 
          }
        }
      }

      error = deserializeJson(doc, config.deliveryCodes);
      if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
          if (String(obj["code"]) == String(codeStr)) {
            Serial.println("Delivery code matched. State -> OPENING_TO_PARCEL");
            currentState = OPENING_TO_PARCEL;
            return;
          }
        }
      }
    }
  }
}

void loopWebServer() {
  // No need to do anything here for AsyncWebServer
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("MQTT message received on topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);

  if (String(topic) == "paketkasten/command") {
    if (message == "OPEN_PARCEL") {
      if (currentState == LOCKED) {
        Serial.println("MQTT command: OPEN_PARCEL. State -> OPENING_TO_PARCEL");
        currentState = OPENING_TO_PARCEL;
      }
    } else if (message == "OPEN_MAIL") {
      if (currentState == LOCKED) {
        Serial.println("MQTT command: OPEN_MAIL. State -> OPENING_TO_TO_MAIL");
        currentState = OPENING_TO_MAIL;
      }
    } else if (message == "CLOSE") {
      if (currentState == PARCEL_OPEN || currentState == MAIL_OPEN) {
        Serial.println("MQTT command: CLOSE. State -> LOCKING");
        currentState = LOCKING;
      }
    }
  }
}

void publishState() {
  if (mqttClient.connected()) {
    String stateTopic = "paketkasten/state";
    String state = getMailboxStateString();
    Serial.print("Publishing state to MQTT: ");
    Serial.println(state);
    mqttClient.publish(stateTopic.c_str(), state.c_str());
  }
}

String getMailboxStateString() {
  switch (currentState) {
    case LOCKED:
      return "LOCKED";
    case OPENING_TO_PARCEL:
      return "OPENING_TO_PARCEL";
    case PARCEL_OPEN:
      return "PARCEL_OPEN";
    case OPENING_TO_MAIL:
      return "OPENING_TO_MAIL";
    case MAIL_OPEN:
      return "MAIL_OPEN";
    case LOCKING:
      return "LOCKING";
    case MOTOR_ERROR:
      return "MOTOR_ERROR";
    default:
      return "UNKNOWN";
  }
}

void loopMqtt() {
  if (config.mqttServer != "") {
    if (!mqttClient.connected()) {
      long now = millis();
      static long lastReconnectAttempt = 0;
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        Serial.println("Attempting MQTT connection...");
        if (mqttClient.connect("Paketkasten", config.mqttUser.c_str(), config.mqttPassword.c_str())) {
          Serial.println("MQTT connected.");
          mqttClient.subscribe("paketkasten/command");
          publishState();
        } else {
          Serial.print("MQTT connection failed, rc=");
          Serial.println(mqttClient.state());
        }
      }
    }
  }
}
