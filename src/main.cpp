#include <Arduino.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "local-dev"
#endif

#include "state.h"
#include "ConfigManager.h"
#include "MelodyPlayer.h"
#include "MotorController.h"
#include "LedController.h"
#include "SwitchManager.h"
#include "WiegandManager.h"
#include "MqttManager.h"
#include "MailboxNetworkManager.h"
#include "AccessControl.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Global configurations
int debounceDelay = 5; // in ms
const bool INVERT_SWITCH_STATE = false;

// Function declarations
void triggerCallback(const char* compartment);
void receivedWiegandCode(char* code, uint8_t bits);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void appTask(void* param);
void mqttTask(void* param);

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

  configManager.begin();
  Serial.println("Configuration loaded.");
  
  motorController.begin();
  Serial.println("Motor setup complete.");
  
  ledController.begin();
  Serial.println("LEDs setup complete.");
  
  switchManager.begin(debounceDelay, INVERT_SWITCH_STATE);
  Serial.println("Switches setup complete.");
  
  wiegandManager.begin(receivedWiegandCode);
  Serial.println("Wiegand setup complete.");
  
  melodyPlayer.begin();
  Serial.println("Buzzer setup complete.");
  
  mailboxNetworkManager.begin();
  Serial.println("Web server setup complete.");

  
  mqttManager.begin(mqttCallback);
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
#ifdef WOKWI_SIMULATION
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      Serial.printf("[Wokwi Sim] Simulating Wiegand Code entry: %s\n", input.c_str());
      input.toUpperCase();
      char tempCode[32];
      strncpy(tempCode, input.c_str(), sizeof(tempCode) - 1);
      tempCode[sizeof(tempCode) - 1] = '\0';
      receivedWiegandCode(tempCode, 26);
    }
  }
  delay(50); // Prevent CPU hogging
#else
  // All work is done in FreeRTOS tasks. Keep loop() alive but idle.
  vTaskDelay(portMAX_DELAY);
#endif
}

// Application logic task — pinned to Core 1 (APP_CPU)
void appTask(void* param) {
  for (;;) {
    if (currentState == PRE_OPENING_TO_PARCEL && (millis() - preOpeningStateEnterTime > OPENING_DELAY_MS)) {
      currentState = OPENING_TO_PARCEL;
    }
    if (currentState == PRE_OPENING_TO_MAIL && (millis() - preOpeningStateEnterTime > OPENING_DELAY_MS)) {
      currentState = OPENING_TO_MAIL;
    }

    switchManager.update();
    motorController.update();
    ledController.update();
    melodyPlayer.update();

    if (currentState != MOTOR_ERROR) {
      bool shouldLock = false;

      if ((currentState == MAIL_OPEN || currentState == PARCEL_OPEN) && (millis() - openStateEnterTime > 1000)) {
        Serial.println("Regular Lock");
        shouldLock = true;
      }

      if (configManager.getConfig().autolock) {
        static unsigned long noSwitchActiveSince = 0;
        bool anySwitchActive = switchManager.isClosedPressed() || switchManager.isParcelPressed() || switchManager.isMailPressed();

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
        if (currentState == LOCKED && !switchManager.isClosedPressed()) {
          Serial.println("Default Lock");
          shouldLock = true;
        }
      }

      if (shouldLock) {
        currentState = LOCKING;
      }
    }

    // Delayed Wiegand attachment to prevent motor braking noise from causing false scans
    if (currentState == LOCKED && !wiegandManager.isAttached() && (millis() - lockedStateEnterTime > 500)) {
      wiegandManager.attach();
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
void mqttTask(void* param) {
  for (;;) {
    MailboxState localState = currentState;
    if (localState == LOCKED || localState == MOTOR_ERROR) {
      mqttManager.update();
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // MQTT doesn't need sub-ms timing
  }
}

void triggerCallback(const char* compartment) {
  Config& config = configManager.getConfig();
  if (config.callbackUrl.length() > 0) {
    String url = config.callbackUrl;
    url.replace("{compartment}", compartment);
    HTTPClient http;
    bool beginSuccess = false;

    if (url.startsWith("https://")) {
      WiFiClientSecure client;
      if (config.callbackSkipCertVal) {
        Serial.println("HTTPS Callback: Skipping certificate validation (Insecure)");
        client.setInsecure();
      } else {
        String caCert = "";
        if (LittleFS.exists("/certs/callback_ca.pem")) {
          File file = LittleFS.open("/certs/callback_ca.pem", "r");
          if (file) {
            caCert = file.readString();
            file.close();
          }
        }
        if (caCert.length() > 0) {
          Serial.println("HTTPS Callback: Setting Root CA certificate");
          client.setCACert(caCert.c_str());
        } else {
          Serial.println("HTTPS Callback Warning: HTTPS requested but no CA certificate found. Falling back to insecure mode.");
          client.setInsecure();
        }
      }
      beginSuccess = http.begin(client, url);
    } else {
      WiFiClient client;
      beginSuccess = http.begin(client, url);
    }

    if (beginSuccess) {
      int httpCode = http.GET();
      if (httpCode > 0) {
        String payload = http.getString();
        Serial.printf("Callback response code: %d\n", httpCode);
        Serial.println(payload);
      } else {
        Serial.printf("Error on HTTP request: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();
    } else {
      Serial.println("Failed to initiate connection in HTTPClient");
    }
  }
}

void requestParcelOpening(const char* requester) {
  if (currentState == LOCKED) {
    if (strcmp(requester, "webinterface") == 0 || strcmp(requester, "mqtt") == 0) {
      configManager.resetDeliveryBlockIfNeeded(requester);
    }
    triggerCallback("parcel");
    Serial.println("Request: OPEN_PARCEL. State -> PRE_OPENING_TO_PARCEL");
    wiegandManager.detach();
    strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
    melodyPlayer.play(configManager.getConfig().selectedMelody);
    preOpeningStateEnterTime = millis();
    currentState = PRE_OPENING_TO_PARCEL;
  }
}

void requestMailOpening(const char* requester) {
  if (currentState == LOCKED) {
    if (strcmp(requester, "webinterface") == 0 || strcmp(requester, "mqtt") == 0) {
      configManager.resetDeliveryBlockIfNeeded(requester);
    }
    triggerCallback("mail");
    Serial.println("Request: OPEN_MAIL. State -> PRE_OPENING_TO_MAIL");
    wiegandManager.detach();
    strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
    melodyPlayer.play(configManager.getConfig().selectedMelody);
    preOpeningStateEnterTime = millis();
    currentState = PRE_OPENING_TO_MAIL;
  }
}

void receivedWiegandCode(char* code, uint8_t bits) {
  Serial.print("Wiegand code received: ");
  Serial.println(code);
  if (bits == 4 || bits == 8) {
    strncpy(lastKeypadCode, code, sizeof(lastKeypadCode) - 1);
    lastKeypadCode[sizeof(lastKeypadCode) - 1] = '\0';
  } else {
    strncpy(lastScannedWiegandId, code, sizeof(lastScannedWiegandId) - 1);
    lastScannedWiegandId[sizeof(lastScannedWiegandId) - 1] = '\0';
  }

  if (currentState == LOCKED) {
    std::string labelOut;
    Config& config = configManager.getConfig();
    AccessType result = AccessControl::evaluate(code, config.ownerCodes.c_str(), config.deliveryCodes.c_str(), &labelOut);
    
    if (result == AccessType::OPEN_MAIL) {
      if (config.oneTimeOpening && deliveryBlocked) {
        deliveryBlocked = false;
        configManager.save();
        Serial.printf("Delivery block reset by owner card scan (%s)\n", labelOut.c_str());
      }
      requestMailOpening(labelOut.c_str());
      return;
    }
    
    // Check one-time codes next
    String otcLabel;
    if (configManager.checkAndRedeemOneTimeCode(code, otcLabel)) {
      if (config.oneTimeOpening) {
        deliveryBlocked = true;
        configManager.save();
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
        configManager.save();
        Serial.println("One-time opening delivery block activated (delivery code used).");
      }
      requestParcelOpening(labelOut.c_str());
      return;
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
    if (message == "OPEN_PARCEL") {
      requestParcelOpening("mqtt");
    } else if (message == "OPEN_MAIL") {
      requestMailOpening("mqtt");
    }
  }
}

void publishState() {
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

