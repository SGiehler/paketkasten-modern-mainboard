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
#include <Bounce2.h>

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
const int BUZZER_PIN = 21;

// Bounce Buttons
Bounce2::Button closedSwitch = Bounce2::Button();
Bounce2::Button parcelSwitch = Bounce2::Button();
Bounce2::Button mailSwitch = Bounce2::Button();
int debounceDelay = 1; // in ms

// Inverting flags
const bool INVERT_SWITCH_STATE = false;
const char* SOFTAP_PASSWORD = "tuVlZfEr5^p!fcUH0QWU";

// Movement Settings
const int PWM_FREQ = 50000;
const int FULL_POWER_MS = 20;
const int FULL_POWER_DUTY_CYCLE = 160;
const int RAMP_DOWN_MS = 10;

MailboxState currentState = LOCKED;
Wiegand wiegand;
AsyncWebServer server(80);
Preferences preferences;
Config config;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
char lastScannedWiegandId[20] = "";
char lastUsed[50] = "unknown";
unsigned long openStateEnterTime = 0;
unsigned long motorStartTime = 0;


// Function declarations
void setupMotor();
void setupLeds();
void setupSwitches();
void setupWiegand();
void setupWebServer();
void setupMqtt();
void setupBuzzer();

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
void playMelody();


void setup() {
  setCpuFrequencyMhz(160);
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
  setupBuzzer();
  Serial.println("Buzzer setup complete.");
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
  config.dutyCycleOpen = preferences.getInt(DUTY_CYCLE_OPEN_KEY, 120);
  config.dutyCycleClose = preferences.getInt(DUTY_CYCLE_CLOSE_KEY, 90);
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
  closedSwitch.attach( CLOSED_SWITCH_PIN, INPUT );
  parcelSwitch.attach( PARCEL_SWITCH_PIN, INPUT );
  mailSwitch.attach( MAIL_SWITCH_PIN, INPUT );

  closedSwitch.interval( debounceDelay );
  parcelSwitch.interval( debounceDelay );
  mailSwitch.interval( debounceDelay );

  closedSwitch.setPressedState( (INVERT_SWITCH_STATE ? HIGH : LOW) );
  parcelSwitch.setPressedState( (INVERT_SWITCH_STATE ? HIGH : LOW) );
  mailSwitch.setPressedState( (INVERT_SWITCH_STATE ? HIGH : LOW) );
}

void setupWiegand() {
  wiegand.begin(WIEGAND_D0_PIN, WIEGAND_D1_PIN);
}

void setupBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
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
    request->send(LittleFS, "/saved.html", "text/html");
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
          strncpy(lastUsed, "webinterface", sizeof(lastUsed) - 1);
          currentState = OPENING_TO_PARCEL;
          playMelody();
        }
      } else if (type == "all") {
        if (currentState == LOCKED) {
          Serial.println("Web command: OPEN_ALL. State -> OPENING_TO_MAIL");
          strncpy(lastUsed, "webinterface", sizeof(lastUsed) - 1);
          currentState = OPENING_TO_MAIL;
          playMelody();
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
  static MailboxState lastMotorState = (MailboxState)-1;

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
        dutyCycle = FULL_POWER_DUTY_CYCLE;
      } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) {
        dutyCycle = 255 - (255 - config.dutyCycleOpen) * (elapsedTime - 100) / RAMP_DOWN_MS;
      }

      analogWrite(MOTOR_PIN_1, dutyCycle);
      analogWrite(MOTOR_PIN_2, 0);

      break;
    case LOCKING:
      dutyCycle = config.dutyCycleClose;
      if (elapsedTime < FULL_POWER_MS) {
        dutyCycle = FULL_POWER_DUTY_CYCLE;
      } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) { 
        dutyCycle = 255 - (255 - config.dutyCycleClose) * (elapsedTime - 50) / RAMP_DOWN_MS;
      }

      analogWrite(MOTOR_PIN_1, 0);
      analogWrite(MOTOR_PIN_2, dutyCycle);

      break;
    case LOCKED:
    case PARCEL_OPEN:
    case MAIL_OPEN:
    case MOTOR_ERROR:
      analogWrite(MOTOR_PIN_1, 255);
      analogWrite(MOTOR_PIN_2, 255);
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
  closedSwitch.update();
  parcelSwitch.update();
  mailSwitch.update();
  static MailboxState lastState = currentState;

  switch (currentState) {
    case OPENING_TO_PARCEL:
      if (parcelSwitch.pressed()) {
        currentState = PARCEL_OPEN;
        openStateEnterTime = millis();
      }
      break;
    case OPENING_TO_MAIL:
      if (mailSwitch.pressed()) {
        currentState = MAIL_OPEN;
        openStateEnterTime = millis();
      }
      break;
    case LOCKING:
      if (closedSwitch.pressed()) {
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
    lastScannedWiegandId[sizeof(lastScannedWiegandId) - 1] = ' ';

    if (currentState == LOCKED) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, config.ownerCodes);

      if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
          if (String(obj["code"]) == String(codeStr)) {
            Serial.println("Owner code matched. State -> OPENING_TO_MAIL");
            strncpy(lastUsed, obj["label"], sizeof(lastUsed) - 1);
            currentState = OPENING_TO_MAIL;
            playMelody();
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
            strncpy(lastUsed, obj["label"], sizeof(lastUsed) - 1);
            currentState = OPENING_TO_PARCEL;
            playMelody();
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
        strncpy(lastUsed, "mqtt", sizeof(lastUsed) - 1);
        currentState = OPENING_TO_PARCEL;
        playMelody();
      }
    } else if (message == "OPEN_MAIL") {
      if (currentState == LOCKED) {
        Serial.println("MQTT command: OPEN_MAIL. State -> OPENING_TO_TO_MAIL");
        strncpy(lastUsed, "mqtt", sizeof(lastUsed) - 1);
        currentState = OPENING_TO_MAIL;
        playMelody();
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
    JsonDocument doc;
    doc["state"] = getMailboxStateString();
    doc["last_used"] = lastUsed;
    String output;
    serializeJson(doc, output);
    Serial.print("Publishing state to MQTT: ");
    Serial.println(output);
    mqttClient.publish(stateTopic.c_str(), output.c_str());
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

#define NOTE_B0 31
#define NOTE_C1 33
#define NOTE_CS1 35
#define NOTE_D1 37
#define NOTE_DS1 39
#define NOTE_E1 41
#define NOTE_F1 44
#define NOTE_FS1 46
#define NOTE_G1 49
#define NOTE_GS1 52
#define NOTE_A1 55
#define NOTE_AS1 58
#define NOTE_B1 62
#define NOTE_C2 65
#define NOTE_CS2 69
#define NOTE_D2 73
#define NOTE_DS2 78
#define NOTE_E2 82
#define NOTE_F2 87
#define NOTE_FS2 93
#define NOTE_G2 98
#define NOTE_GS2 104
#define NOTE_A2 110
#define NOTE_AS2 117
#define NOTE_B2 123
#define NOTE_C3 131
#define NOTE_CS3 139
#define NOTE_D3 147
#define NOTE_DS3 156
#define NOTE_E3 165
#define NOTE_F3 175
#define NOTE_FS3 185
#define NOTE_G3 196
#define NOTE_GS3 208
#define NOTE_A3 220
#define NOTE_AS3 233
#define NOTE_B3 247
#define NOTE_C4 262
#define NOTE_CS4 277
#define NOTE_D4 294
#define NOTE_DS4 311
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_FS4 370
#define NOTE_G4 392
#define NOTE_GS4 415
#define NOTE_A4 440
#define NOTE_AS4 466
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_CS5 554
#define NOTE_D5 587
#define NOTE_DS5 622
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_FS5 740
#define NOTE_G5 784
#define NOTE_GS5 831
#define NOTE_A5 880
#define NOTE_AS5 932
#define NOTE_B5 988
#define NOTE_C6 1047
#define NOTE_CS6 1109
#define NOTE_D6 1175
#define NOTE_DS6 1245
#define NOTE_E6 1319
#define NOTE_F6 1397
#define NOTE_FS6 1480
#define NOTE_G6 1568
#define NOTE_GS6 1661
#define NOTE_A6 1760
#define NOTE_AS6 1865
#define NOTE_B6 1976
#define NOTE_C7 2093
#define NOTE_CS7 2217
#define NOTE_D7 2349
#define NOTE_DS7 2489
#define NOTE_E7 2637
#define NOTE_F7 2794
#define NOTE_FS7 2960
#define NOTE_G7 3136
#define NOTE_GS7 3322
#define NOTE_A7 3520
#define NOTE_AS7 3729
#define NOTE_B7 3951
#define NOTE_C8 4186
#define NOTE_CS8 4435
#define NOTE_D8 4699
#define NOTE_DS8 4978
#define REST 0

// Underworld melody
int underworld_melody[] = {
  NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
  NOTE_AS3, NOTE_AS4, 0,
  0,
  NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
  NOTE_AS3, NOTE_AS4, 0,
  0,
  NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
  NOTE_DS3, NOTE_DS4, 0,
  0,
  NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
  NOTE_DS3, NOTE_DS4, 0,
  0, NOTE_DS4, NOTE_CS4, NOTE_D4,
  NOTE_CS4, NOTE_DS4,
  NOTE_DS4, NOTE_GS3,
  NOTE_G3, NOTE_CS4,
  NOTE_C4, NOTE_FS4, NOTE_F4, NOTE_E3, NOTE_AS4, NOTE_A4,
  NOTE_GS4, NOTE_DS4, NOTE_B3,
  NOTE_AS3, NOTE_A3, NOTE_GS3,
  0, 0, 0
};
//Underworld tempo
int underworld_tempo[] = {
  12, 12, 12, 12,
  12, 12, 6,
  3,
  12, 12, 12, 12,
  12, 12, 6,
  3,
  12, 12, 12, 12,
  12, 12, 6,
  3,
  12, 12, 12, 12,
  12, 12, 6,
  6, 18, 18, 18,
  6, 6,
  6, 6,
  6, 6,
  18, 18, 18, 18, 18, 18,
  10, 10, 10,
  10, 10, 10,
  3, 3, 3
};

void playMelody() {
  int size = sizeof(underworld_melody) / sizeof(int);
  for (int thisNote = 0; thisNote < size; thisNote++) {
    // to calculate the note duration, take one second divided by the note type.
    //e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
    int noteDuration = 1000 / underworld_tempo[thisNote];
    tone(BUZZER_PIN, underworld_melody[thisNote], noteDuration);
    // to distinguish the notes, set a minimum time between them.
    // the note's duration + 30% seems to work well:
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    // stop the tone playing:
    noTone(BUZZER_PIN);
  }
}
