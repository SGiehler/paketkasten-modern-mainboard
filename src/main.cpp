#include <Arduino.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "local-dev"
#endif

#include "state.h"
#include "config.h"
#include "melodies.h"
#include "AccessControl.h"
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
#include <vector>
#include <Update.h>
#include <HTTPClient.h>

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
const char* SOFTAP_PASSWORD = "G67zC4OiB";

// Movement Settings
const int PWM_FREQ = 50000;
const int FULL_POWER_MS = 100;
const int FULL_POWER_DUTY_CYCLE = 200;
const int RAMP_DOWN_MS = 10;
const int OPENING_DELAY_MS = 700;

volatile MailboxState currentState = LOCKED;
Wiegand wiegand;
AsyncWebServer server(80);
Preferences preferences;
Config config;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
char lastScannedWiegandId[20] = "";
char lastUsed[50] = "unknown";
unsigned long openStateEnterTime = 0;
unsigned long preOpeningStateEnterTime = 0;
unsigned long motorStartTime = 0;
unsigned long lockedStateEnterTime = 0;
bool wiegandAttached = true;
volatile bool shouldRestart = false;
volatile bool deliveryBlocked = false;
std::vector<String> mqttMessageQueue;

// Mutex to protect ONLY cross-core shared data (Core 1 <-> Core 0)
// Specifically protects: mqttMessageQueue (written on Core 1, read on Core 0)
// NOT used for Core-1-only state (currentState, etc.) to avoid priority inversion
SemaphoreHandle_t mqttQueueMutex;

// Melody playback variables
int* currentMelody = nullptr;
int* currentTempo = nullptr;
int melodySize = 0;
int currentNoteIndex = 0;
unsigned long noteStartTime = 0;
int noteDuration = 0;
bool melodyPlaying = false;
int wholenote = 1000;

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
void loopMqtt();

// FreeRTOS task functions
void appTask(void* param);
void mqttTask(void* param);

String getMailboxStateString();
void receivedWiegandCode(char* code, uint8_t bits);
void loadConfiguration();
void saveConfiguration();
void resetDeliveryBlockIfNeeded(const char* requester);
bool checkAndRedeemOneTimeCode(const char* scannedCode, String& labelOut);
void publishState();
void handleMqttQueue();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void startMelodyPlayback(String melodyName);
void loopMelody();
void factoryReset();
void triggerCallback(const char* compartment);
void requestParcelOpening(const char* requester);
void requestMailOpening(const char* requester);
void opensequenceDone();

void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  Serial.println("Booting...");
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  // Create mutex for cross-core MQTT queue protection only
  mqttQueueMutex = xSemaphoreCreateMutex();

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

  // Pin application logic to Core 1 (APP_CPU, away from WiFi on Core 0)
  xTaskCreatePinnedToCore(
    appTask,
    "AppTask",
    8192,             // Stack size
    NULL,
    1,                // Priority (same as default loop)
    NULL,
    1                 // Core 1 (APP_CPU)
  );

  // Pin MQTT handling to Core 0 (PRO_CPU, alongside WiFi)
  xTaskCreatePinnedToCore(
    mqttTask,
    "MqttTask",
    4096,             // Stack size
    NULL,
    1,                // Priority
    NULL,
    0                 // Core 0 (PRO_CPU)
  );

  Serial.println("Setup complete. Tasks pinned: AppTask->Core1, WiegandTask->Core1, MqttTask->Core0");
}

void loop() {
  // All work is done in FreeRTOS tasks. Keep loop() alive but idle.
  vTaskDelay(portMAX_DELAY);
}

// Application logic task — pinned to Core 1 (APP_CPU)
// Handles: state machine, motor, LEDs, switches, melody, restart
// No mutex needed here — WiegandTask is also on Core 1, so FreeRTOS
// preemptive scheduling handles concurrency without blocking.
void appTask(void* param) {
  for (;;) {
    if (currentState == PRE_OPENING_TO_PARCEL && (millis() - preOpeningStateEnterTime > OPENING_DELAY_MS)) {
      currentState = OPENING_TO_PARCEL;
    }
    if (currentState == PRE_OPENING_TO_MAIL && (millis() - preOpeningStateEnterTime > OPENING_DELAY_MS)) {
      currentState = OPENING_TO_MAIL;
    }

    loopSwitches();
    loopMotor();
    loopLeds();
    loopMelody();

    if (currentState != MOTOR_ERROR) {
      bool shouldLock = false;

      if ((currentState == MAIL_OPEN || currentState == PARCEL_OPEN) && (millis() - openStateEnterTime > 1000)) {
        Serial.println("Regular Lock");
        shouldLock = true;
      }

      if (config.autolock) {
        static unsigned long noSwitchActiveSince = 0;
        bool anySwitchActive = closedSwitch.isPressed() || parcelSwitch.isPressed() || mailSwitch.isPressed();

        if (anySwitchActive) {
          noSwitchActiveSince = 0;
        } else {
          if (noSwitchActiveSince == 0) {
            noSwitchActiveSince = millis();
          }
        }

        if (noSwitchActiveSince != 0 && (millis() - noSwitchActiveSince > 10000)) {
          if (currentState != OPENING_TO_PARCEL && currentState != OPENING_TO_MAIL && currentState != LOCKING) {
            Serial.println("No switch Lock");
            shouldLock = true;
          }
        }

        // if locked is the current state but the locked switch isn't pressed set locking.
        if (currentState == LOCKED && !closedSwitch.isPressed()) {
          Serial.println("Default Lock");
          shouldLock = true;
        }
      }

      if (shouldLock) {
        currentState = LOCKING;
      }
    }

    // Delayed Wiegand attachment to prevent motor braking noise from causing false scans
    if (currentState == LOCKED && !wiegandAttached && (millis() - lockedStateEnterTime > 500)) {
      wiegand.attach();
      wiegandAttached = true;
      Serial.println("Wiegand reader re-attached after motor noise cooldown");
    }
 
    if (shouldRestart) {
      delay(100);
      ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(1)); // 1ms tick for responsive motor/switch control
  }
}

// MQTT task — pinned to Core 0 (PRO_CPU, alongside WiFi)
// Reads currentState (volatile enum, atomic on ESP32) without mutex.
// Only uses mqttQueueMutex when draining the message queue.
void mqttTask(void* param) {
  for (;;) {
    MailboxState localState = currentState;
    if (localState == LOCKED || localState == MOTOR_ERROR) {
      loopMqtt();
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // MQTT doesn't need sub-ms timing
  }
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
  config.dutyCycleClose = preferences.getInt(DUTY_CYCLE_CLOSE_KEY, 20);
  config.selectedMelody = preferences.getString(SELECTED_MELODY_KEY, "NOKIA_TUNE");
  config.callbackUrl = preferences.getString(CALLBACK_URL_KEY, "");
  config.autolock = preferences.getBool(AUTOLOCK_KEY, true);
  config.oneTimeCodes = preferences.getString(ONE_TIME_CODES_KEY, "[]");
  config.oneTimeOpening = preferences.getBool(ONE_TIME_OPENING_KEY, false);
  deliveryBlocked = preferences.getBool(DELIVERY_BLOCKED_KEY, false);
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
  preferences.putString(SELECTED_MELODY_KEY, config.selectedMelody);
  preferences.putString(CALLBACK_URL_KEY, config.callbackUrl);
  preferences.putBool(AUTOLOCK_KEY, config.autolock);
  preferences.putString(ONE_TIME_CODES_KEY, config.oneTimeCodes);
  preferences.putBool(ONE_TIME_OPENING_KEY, config.oneTimeOpening);
  preferences.putBool(DELIVERY_BLOCKED_KEY, deliveryBlocked);
  preferences.end();
}

void factoryReset() {
  preferences.begin(PREFERENCES_NAMESPACE, false);
  preferences.clear();
  preferences.end();
  Serial.println("All preferences cleared.");
}

void resetDeliveryBlockIfNeeded(const char* requester) {
  if (config.oneTimeOpening && deliveryBlocked) {
    deliveryBlocked = false;
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.putBool(DELIVERY_BLOCKED_KEY, false);
    preferences.end();
    Serial.printf("Delivery block reset by owner (%s)\n", requester);
  }
}

bool checkAndRedeemOneTimeCode(const char* scannedCode, String& labelOut) {
  if (config.oneTimeCodes.length() == 0 || config.oneTimeCodes == "[]") {
    return false;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, config.oneTimeCodes);
  if (error) {
    Serial.println("Error parsing one-time codes JSON");
    return false;
  }
  JsonArray array = doc.as<JsonArray>();
  bool found = false;
  for (JsonObject obj : array) {
    const char* code = obj["code"];
    bool redeemed = obj["redeemed"] | false;
    if (code && strcmp(code, scannedCode) == 0) {
      if (redeemed) {
        Serial.println("One-time code matched but already redeemed.");
        return false;
      }
      
      // Found an active code! Now check if we are delivery blocked.
      if (config.oneTimeOpening && deliveryBlocked) {
        Serial.println("Access denied: One-time opening active and delivery blocked. Code NOT redeemed.");
        return false; // Keep code active!
      }
      
      labelOut = obj["label"] | "One-Time Code";
      obj["redeemed"] = true;
      obj["redeemedAt"] = millis(); // Store redemption timestamp
      found = true;
      break;
    }
  }
  if (found) {
    String updatedJson;
    serializeJson(doc, updatedJson);
    config.oneTimeCodes = updatedJson;
    
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.putString(ONE_TIME_CODES_KEY, config.oneTimeCodes);
    preferences.end();
    Serial.printf("One-time code redeemed: %s\n", labelOut.c_str());
    return true;
  }
  return false;
}

void triggerCallback(const char* compartment) {
  if (config.callbackUrl.length() > 0) {
    String url = config.callbackUrl;
    url.replace("{compartment}", compartment);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println(httpCode);
      Serial.println(payload);
    } else {
      Serial.println("Error on HTTP request");
    }
    http.end();
  }
}

void requestParcelOpening(const char* requester) {
  // Called from Wiegand task (Core 1), MQTT callback (Core 0), and web handler.
  // Mutex is already held when called from appTask; use tryTake for cross-core callers.
  if (currentState == LOCKED) {
    if (strcmp(requester, "webinterface") == 0 || strcmp(requester, "mqtt") == 0) {
      resetDeliveryBlockIfNeeded(requester);
    }
    triggerCallback("parcel");
    Serial.println("Request: OPEN_PARCEL. State -> PRE_OPENING_TO_PARCEL");
    wiegand.detach();
    wiegandAttached = false;
    strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
    startMelodyPlayback(config.selectedMelody);
    preOpeningStateEnterTime = millis();
    currentState = PRE_OPENING_TO_PARCEL;
  }
}

void requestMailOpening(const char* requester) {
  // Called from Wiegand task (Core 1), MQTT callback (Core 0), and web handler.
  if (currentState == LOCKED) {
    if (strcmp(requester, "webinterface") == 0 || strcmp(requester, "mqtt") == 0) {
      resetDeliveryBlockIfNeeded(requester);
    }
    triggerCallback("mail");
    Serial.println("Request: OPEN_MAIL. State -> PRE_OPENING_TO_MAIL");
    wiegand.detach();
    wiegandAttached = false;
    strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
    startMelodyPlayback(config.selectedMelody);
    preOpeningStateEnterTime = millis();
    currentState = PRE_OPENING_TO_MAIL;
  }
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
  // Create a dedicated FreeRTOS task for Wiegand processing
  // Pinned to Core 1 (APP_CPU), polls every 10ms
  xTaskCreatePinnedToCore(
    [](void* arg) {
      wiegand.begin(WIEGAND_D0_PIN, WIEGAND_D1_PIN);
      for (;;) {
        loopWiegand();
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    },
    "WiegandTask",
    4096,             // Stack size
    NULL,             // Task parameters
    2,                // Priority (2 is higher than appTask priority 1)
    NULL,             // Task handle
    1                 // Core 1 (APP_CPU)
  );
}

void setupBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/update.html", "text/html");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
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
    doc["selectedMelody"] = config.selectedMelody;
    doc["callbackUrl"] = config.callbackUrl;
    doc["autolock"] = config.autolock;
    doc["oneTimeCodes"] = config.oneTimeCodes;
    doc["oneTimeOpening"] = config.oneTimeOpening;
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
    config.selectedMelody = request->arg("selectedMelody");
    config.callbackUrl = request->arg("callbackUrl");
    config.autolock = request->hasArg("autolock");
    config.oneTimeOpening = request->hasArg("oneTimeOpening");
    saveConfiguration();
    delay(500);
    Serial.println("Configuration saved. Restarting...");
    request->send(200, "text/plain", "OK");
    delay(2000);
    shouldRestart = true;
  });

  server.on("/factoryreset", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Factory reset requested.");
    factoryReset();
    loadConfiguration();
    request->send(200, "text/plain", "OK");
    delay(2000);
    shouldRestart = true;
  });

  server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request){
    String jsonResponse;
    JsonDocument doc;
    doc["wiegand_id"] = lastScannedWiegandId;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["mailbox_state"] = getMailboxStateString();
    doc["closed_switch"] = digitalRead(CLOSED_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
    doc["parcel_switch"] = digitalRead(PARCEL_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
    doc["mail_switch"] = digitalRead(MAIL_SWITCH_PIN) == (INVERT_SWITCH_STATE ? HIGH : LOW);
    doc["delivery_blocked"] = deliveryBlocked;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  server.on("/save-onetime-codes", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("oneTimeCodes", true)) {
      config.oneTimeCodes = request->getParam("oneTimeCodes", true)->value();
      
      preferences.begin(PREFERENCES_NAMESPACE, false);
      preferences.putString(ONE_TIME_CODES_KEY, config.oneTimeCodes);
      preferences.end();
      
      Serial.println("One-time codes saved dynamically.");
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/playMelody", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("melody", true)) {
      String melodyType = request->getParam("melody", true)->value();
      Serial.print("Playing melody: ");
      Serial.println(melodyType);
      startMelodyPlayback(melodyType);
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/open", HTTP_POST, [](AsyncWebServerRequest *request){
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
      noInterrupts();
      analogWrite(MOTOR_PIN_1, dutyCycle);
      analogWrite(MOTOR_PIN_2, 0);
      interrupts();
      break;
    case LOCKING:
      dutyCycle = config.dutyCycleClose;
      if (elapsedTime < FULL_POWER_MS) {
        dutyCycle = FULL_POWER_DUTY_CYCLE;
      } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) { 
        dutyCycle = 255 - (255 - config.dutyCycleClose) * (elapsedTime - 50) / RAMP_DOWN_MS;
      }
      noInterrupts();
      analogWrite(MOTOR_PIN_1, 0);
      analogWrite(MOTOR_PIN_2, dutyCycle);
      interrupts();
      break;
    case LOCKED:
    case PARCEL_OPEN:
    case MAIL_OPEN:
    case MOTOR_ERROR:
      noInterrupts();
      analogWrite(MOTOR_PIN_1, 255);
      analogWrite(MOTOR_PIN_2, 255);
      interrupts();
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
    case PRE_OPENING_TO_PARCEL:
    case OPENING_TO_PARCEL:
    case PRE_OPENING_TO_MAIL:
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
        lockedStateEnterTime = millis();
        wiegandAttached = false;
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
  // No mutex needed — runs on Core 1 alongside appTask.
  // FreeRTOS preemptive scheduling (WiegandTask priority 2 > appTask priority 1)
  // ensures this task preempts appTask without blocking.
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
      std::string labelOut;
      AccessType result = AccessControl::evaluate(codeStr, config.ownerCodes.c_str(), config.deliveryCodes.c_str(), &labelOut);
      
      if (result == AccessType::OPEN_MAIL) {
        if (config.oneTimeOpening && deliveryBlocked) {
          deliveryBlocked = false;
          preferences.begin(PREFERENCES_NAMESPACE, false);
          preferences.putBool(DELIVERY_BLOCKED_KEY, false);
          preferences.end();
          Serial.printf("Delivery block reset by owner card scan (%s)\n", labelOut.c_str());
        }
        requestMailOpening(labelOut.c_str());
        return;
      }
      
      // Check one-time codes next
      String otcLabel;
      if (checkAndRedeemOneTimeCode(codeStr, otcLabel)) {
        if (config.oneTimeOpening) {
          deliveryBlocked = true;
          preferences.begin(PREFERENCES_NAMESPACE, false);
          preferences.putBool(DELIVERY_BLOCKED_KEY, true);
          preferences.end();
          Serial.println("One-time opening delivery block activated (one-time code used).");
        }
        requestParcelOpening(otcLabel.c_str());
        return;
      }
      
      // Check regular delivery codes
      if (result == AccessType::OPEN_PARCEL) {
        if (config.oneTimeOpening && deliveryBlocked) {
          Serial.println("Access denied: One-time opening active and delivery blocked (delivery code).");
          return;
        }
        if (config.oneTimeOpening) {
          deliveryBlocked = true;
          preferences.begin(PREFERENCES_NAMESPACE, false);
          preferences.putBool(DELIVERY_BLOCKED_KEY, true);
          preferences.end();
          Serial.println("One-time opening delivery block activated (delivery code used).");
        }
        requestParcelOpening(labelOut.c_str());
        return;
      }
    }
  }
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
    // MQTT callback runs on Core 0. currentState read is safe (volatile enum,
    // atomic on 32-bit ESP32). The requestXxxOpening functions write to
    // Core-1-owned state, but this is a rare event and the write is atomic.
    if (message == "OPEN_PARCEL") {
      requestParcelOpening("mqtt");
    } else if (message == "OPEN_MAIL") {
      requestMailOpening("mqtt");
    }
  }
}

void handleMqttQueue() {
    // Called from mqttTask on Core 0. Uses mqttQueueMutex to safely read the queue.
    if (mqttClient.connected() && xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (!mqttMessageQueue.empty()) {
            // Copy queue locally and clear, then release mutex before publishing
            std::vector<String> localQueue = mqttMessageQueue;
            mqttMessageQueue.clear();
            xSemaphoreGive(mqttQueueMutex);

            String stateTopic = "paketkasten/state";
            for (const auto& msg : localQueue) {
                Serial.print("Publishing state to MQTT: ");
                Serial.println(msg);
                mqttClient.publish(stateTopic.c_str(), msg.c_str());
            }
        } else {
            xSemaphoreGive(mqttQueueMutex);
        }
    }
}

void publishState() {
  // Called from loopSwitches on Core 1. Uses mqttQueueMutex to safely push.
  JsonDocument doc;
  doc["state"] = getMailboxStateString();
  doc["last_used"] = lastUsed;
  String output;
  serializeJson(doc, output);
  Serial.print("Queuing state for MQTT: ");
  Serial.println(output);
  if (xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    mqttMessageQueue.push_back(output);
    xSemaphoreGive(mqttQueueMutex);
  }
}

String getMailboxStateString() {
  switch (currentState) {
    case LOCKED:
      return "LOCKED";
    case PRE_OPENING_TO_PARCEL:
      return "PRE_OPENING_TO_PARCEL";
    case OPENING_TO_PARCEL:
      return "OPENING_TO_PARCEL";
    case PARCEL_OPEN:
      return "PARCEL_OPEN";
    case PRE_OPENING_TO_MAIL:
      return "PRE_OPENING_TO_MAIL";
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
    mqttClient.loop();
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
    handleMqttQueue();
  }
}

void startMelodyPlayback(String melodyName) {
  currentMelody = nullptr;
  currentTempo = nullptr;
  melodySize = 0;
  currentNoteIndex = 0;
  noteStartTime = 0;
  noteDuration = 0;
  melodyPlaying = false;

  if (melodyName == "NOKIA_TUNE") {
      wholenote = 1333;
      static int nokiaTuneMelody[] = {
        NOTE_E5, NOTE_D5, NOTE_FS4, NOTE_GS4, NOTE_CS5, NOTE_B4, NOTE_D4, NOTE_E4, NOTE_B4, NOTE_A4, NOTE_CS4, NOTE_E4, NOTE_A4
      };
      static int nokiaTuneTempo[] = {
        8, 8, 4, 4, 8, 8, 4, 4, 8, 8, 4, 4, 2
      };
      currentMelody = nokiaTuneMelody;
      currentTempo = nokiaTuneTempo;
      melodySize = sizeof(nokiaTuneMelody) / sizeof(int);
  // Source: https://github.com/robsoncouto/arduino-songs/blob/master/imperialmarch/imperialmarch.ino
  } else if (melodyName == "IMPERIAL_MARCH") {
      wholenote = 1800;
      static int imperialMarchMelody[] = {
        NOTE_A4, NOTE_A4, NOTE_A4, NOTE_F4, NOTE_C5,
        NOTE_A4, NOTE_F4, NOTE_C5, NOTE_A4,
        NOTE_E5, NOTE_E5, NOTE_E5, NOTE_F5, NOTE_C5,
        NOTE_G4, NOTE_F4, NOTE_C5, NOTE_A4
      };
      static int imperialMarchTempo[] = {
        4, 4, 4, 5, 16,
        4, 5, 16, 2,
        4, 4, 4, 5, 16,
        4, 5, 16, 2
      };
      currentMelody = imperialMarchMelody;
      currentTempo = imperialMarchTempo;
      melodySize = sizeof(imperialMarchMelody) / sizeof(int);
  // Source: https://github.com/robsoncouto/arduino-songs/blob/master/supermario/supermario.ino
  } else if (melodyName == "MARIO") {
      wholenote = 1000;
      static int marioMelody[] = {
        NOTE_E5, NOTE_E5, REST, NOTE_E5, REST, NOTE_C5, NOTE_E5, REST,
        NOTE_G5, REST, REST,  REST, NOTE_G4, REST, REST, REST,
        NOTE_C5, REST, REST, NOTE_G4, REST, REST, NOTE_E4, REST,
        REST, NOTE_A4, REST, NOTE_B4, REST, NOTE_AS4, NOTE_A4, REST,
        NOTE_G4, NOTE_E5, NOTE_G5, NOTE_A5, REST, NOTE_F5, NOTE_G5,
        REST, NOTE_E5, REST, NOTE_C5, NOTE_D5, NOTE_B4, REST, REST
      };
      static int marioTempo[] = {
        8, 8, 8, 8, 8, 8, 8, 8,
        4, 8, 8, 8, 4, 8, 8, 8,
        4, 8, 8, 4, 8, 8, 4, 8,
        8, 4, 8, 4, 8, 8, 4, 8,
        8, 8, 8, 8, 8, 8, 8,
        8, 4, 8, 8, 8, 4, 8, 8
      };
      currentMelody = marioMelody;
      currentTempo = marioTempo;
      melodySize = sizeof(marioMelody) / sizeof(int);
  // Source: https://www.princetronics.com/the-simpsons-theme-song-on-arduino/
  } else if (melodyName == "WINDOWS_XP_STARTUP") {
      wholenote = 1000;
      static int windowsXpStartupMelody[] = {
        NOTE_DS5, NOTE_GS4, NOTE_AS4, NOTE_DS5, NOTE_GS4, NOTE_AS4, NOTE_DS5
      };
      static int windowsXpStartupTempo[] = {
        4, 8, 8, 2, 8, 8, 4
      };
      currentMelody = windowsXpStartupMelody;
      currentTempo = windowsXpStartupTempo;
      melodySize = sizeof(windowsXpStartupMelody) / sizeof(int);
  // Source: https://www.hackster.io/jrance/super-mario-theme-song-w-piezo-buzzer-and-arduino-1cc63c
  } else if (melodyName == "INTEL_INSIDE") {
      wholenote = 400;
      static int intelInsideMelody[] = {
        NOTE_DS4, NOTE_DS4, NOTE_GS4, NOTE_DS4, NOTE_AS4
      };
      static int intelInsideTempo[] = {
        2, 2, 2, 2, 1
      };
      currentMelody = intelInsideMelody;
      currentTempo = intelInsideTempo;
      melodySize = sizeof(intelInsideMelody) / sizeof(int);
  // Source: https://github.com/robsoncouto/arduino-songs/blob/master/tetris/tetris.ino
  } else if (melodyName == "TETRIS") {
      wholenote = 1000;
      static int tetrisMelody[] = {
        NOTE_E5, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4,
        NOTE_A4, NOTE_A4, NOTE_C5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4,
        NOTE_C5, NOTE_D5, NOTE_E5, NOTE_C5, NOTE_A4, NOTE_A4, REST,
        NOTE_D5, NOTE_F5, NOTE_A5, NOTE_G5, NOTE_F5, NOTE_E5,
        NOTE_C5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_E5,
        NOTE_C5, NOTE_A4, NOTE_A4
      };
      static int tetrisTempo[] = {
        4, 8, 8, 4, 8, 8, 4, 4,
        4, 8, 8, 4, 8, 8, 4,
        8, 8, 4, 4, 4, 4, 4,
        4, 8, 8, 4, 8, 8, 4,
        8, 8, 4, 8, 8, 4, 8, 8,
        4, 4, 4
      };
      currentMelody = tetrisMelody;
      currentTempo = tetrisTempo;
      melodySize = sizeof(tetrisMelody) / sizeof(int);
  } else {
      wholenote = 2000;
      static int defaultMelody[] = {
        NOTE_CS4, NOTE_D4, REST, NOTE_E4, NOTE_F4, REST, NOTE_CS4, NOTE_D4,
        NOTE_E4, NOTE_F4, NOTE_B3, NOTE_D4, NOTE_CS4, NOTE_A3, NOTE_G3, NOTE_D3, NOTE_E3, NOTE_F3
      };
      static int defaultTempo[] = {
        8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 4, 4, 4, 8, 8, 2
      };
      currentMelody = defaultMelody;
      currentTempo = defaultTempo;
      melodySize = sizeof(defaultMelody) / sizeof(int);
  }

  if (currentMelody == nullptr || currentTempo == nullptr || melodySize == 0) {
    Serial.println("Error: Melody not found or empty.");
    return;
  }
  melodyPlaying = true;
  currentNoteIndex = 0;
  noteStartTime = millis();
  noteDuration = wholenote / currentTempo[currentNoteIndex];
  tone(BUZZER_PIN, currentMelody[currentNoteIndex], noteDuration);
}

void loopMelody() {
  if (!melodyPlaying) {
    return;
  }

  if (millis() - noteStartTime >= noteDuration * 1.20) { // Pause between notes
    noTone(BUZZER_PIN);
    currentNoteIndex++;
    if (currentNoteIndex < melodySize) {
      noteStartTime = millis();
      noteDuration = wholenote / currentTempo[currentNoteIndex];
      tone(BUZZER_PIN, currentMelody[currentNoteIndex], noteDuration);
    } else {
      melodyPlaying = false;
      noTone(BUZZER_PIN);
    }
  }
}
