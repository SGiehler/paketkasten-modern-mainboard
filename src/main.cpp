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
#include "TelemetryLog.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Global configurations
int debounceDelay = 2; // in ms
const bool INVERT_SWITCH_STATE = false;
static constexpr size_t MAX_MQTT_QUEUE_SIZE = 20;
static constexpr unsigned long REQUEST_COOLDOWN_MS = 2500;
static constexpr size_t CALLBACK_ITEM_LEN = 16;

// Function declarations
void triggerCallback(const char* compartment);
void receivedWiegandCode(char* code, uint8_t bits);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void appTask(void* param);
void mqttTask(void* param);

void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  telemetryLog.logf("SYSTEM", "Booting Paketkasten fw=%s heap=%u", FIRMWARE_VERSION, (unsigned)ESP.getFreeHeap());

  if (!LittleFS.begin(false)) {
    telemetryLog.logf("SYSTEM", "LittleFS mount failed; attempting format fallback");
    if (!LittleFS.begin(true)) {
      telemetryLog.logf("SYSTEM", "LittleFS unavailable; fallback web OTA active");
    }
  }

  // Create synchronization primitives for cross-core queues
  mqttQueueMutex = xSemaphoreCreateMutex();
  httpCallbackQueue = xQueueCreate(4, CALLBACK_ITEM_LEN);

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
    &appTaskHandle,   // Limit switch ISRs notify this task to brake immediately
    1                 // Core 1 (APP_CPU)
  );

  // Pin MQTT, WiFi recovery & async HTTP/HTTPS callbacks to Core 0 (PRO_CPU, alongside WiFi).
  // 8192 bytes stack is required for mbedTLS / WiFiClientSecure handshakes.
  xTaskCreatePinnedToCore(
    mqttTask,
    "MqttTask",
    8192,             // Stack size (TLS safe)
    NULL,
    1,                // Priority
    NULL,
    0                 // Core 0 (PRO_CPU)
  );

  telemetryLog.logf("SYSTEM", "Setup complete (AppTask+Wiegand->Core1, MqttTask->Core0, heap=%u)",
                    (unsigned)ESP.getFreeHeap());
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
    unsigned long now = millis();

    if (currentState == PRE_OPENING_TO_PARCEL && (now - preOpeningStateEnterTime > OPENING_DELAY_MS)) {
      portENTER_CRITICAL(&stateMux);
      if (currentState == PRE_OPENING_TO_PARCEL) {
        closedAlreadyReachedInCycle = false;
        currentState = OPENING_TO_PARCEL;
      }
      portEXIT_CRITICAL(&stateMux);
    }
    if (currentState == PRE_OPENING_TO_MAIL && (now - preOpeningStateEnterTime > OPENING_DELAY_MS)) {
      portENTER_CRITICAL(&stateMux);
      if (currentState == PRE_OPENING_TO_MAIL) {
        closedAlreadyReachedInCycle = false;
        currentState = OPENING_TO_MAIL;
      }
      portEXIT_CRITICAL(&stateMux);
    }

    switchManager.update();
    motorController.update();
    ledController.update();
    melodyPlayer.update();

    const MotorSettings settings = configManager.getMotorSettings();
    static unsigned long noSwitchActiveSince = 0;

    if (currentState != MOTOR_ERROR && !otaInProgress) {
      bool shouldLock = false;
      MailboxState st = currentState;

      if ((st == MAIL_OPEN || st == PARCEL_OPEN) && (millis() - openStateEnterTime > 1000)) {
        telemetryLog.logf("MOTOR", "Auto-close trigger (Regular Lock from %s)", mailboxStateName(st));
        shouldLock = true;
      }

      if (settings.autolock) {
        bool closedActive = switchManager.isClosedPressed();
        bool parcelActive = switchManager.isParcelPressed();
        bool mailActive = switchManager.isMailPressed();
        bool anySwitchActive = closedActive || parcelActive || mailActive;

        // If the arm physically moved onto Parcel/Mail while in LOCKED state, allow autolock again.
        if (st == LOCKED && (parcelActive || mailActive)) {
          closedAlreadyReachedInCycle = false;
        }

        if (anySwitchActive) {
          noSwitchActiveSince = 0;
        } else if (noSwitchActiveSince == 0) {
          noSwitchActiveSince = millis();
        }

        // Only trigger "No switch Lock" in resting states (never abort PRE_OPENING_*, OPENING_*, LOCKING)
        // and never drive further into the closed housing stop if HClosed was already reached in this cycle.
        const bool isRestingState = (st == LOCKED || st == PARCEL_OPEN || st == MAIL_OPEN);
        if (isRestingState && !closedAlreadyReachedInCycle &&
            noSwitchActiveSince != 0 && (millis() - noSwitchActiveSince > 10000)) {
          telemetryLog.logf("MOTOR", "Autolock trigger (No switch active >10s)");
          shouldLock = true;
          noSwitchActiveSince = 0;
        }

        // If LOCKED but closed switch isn't active AND we haven't already passed HClosed in this cycle, lock.
        if (st == LOCKED && !closedActive && !closedAlreadyReachedInCycle && (millis() - lockedStateEnterTime > 1000)) {
          telemetryLog.logf("MOTOR", "Autolock trigger (Default Lock: closed sensor inactive)");
          shouldLock = true;
        }
      } else {
        noSwitchActiveSince = 0;
      }

      if (shouldLock) {
        portENTER_CRITICAL(&stateMux);
        if (currentState == LOCKED || currentState == PARCEL_OPEN || currentState == MAIL_OPEN) {
          currentState = LOCKING;
        }
        portEXIT_CRITICAL(&stateMux);
        noSwitchActiveSince = 0;
      }
    } else {
      noSwitchActiveSince = 0;
    }

    // Keep Wiegand detached while unlocked/moving/updating, and re-attach after motor braking noise cooldown
    if ((currentState != LOCKED || otaInProgress) && wiegandManager.isAttached()) {
      wiegandManager.detach();
    } else if (currentState == LOCKED && !otaInProgress && !wiegandManager.isAttached() &&
               (millis() - lockedStateEnterTime > 500)) {
      wiegandManager.attach();
      telemetryLog.logf("WIEGAND", "Reader re-attached after motor cooldown");
    }
 
    if (shouldRestart && (millis() - restartRequestedAt >= restartDelayMs)) {
      telemetryLog.logf("SYSTEM", "Executing scheduled restart");
      ESP.restart();
    }

    // 1 ms tick for responsive motor/switch control; a limit switch ISR wakes us immediately.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
  }
}

// MQTT, WiFi recovery & async HTTP callback task — pinned to Core 0 (PRO_CPU, alongside WiFi)
void mqttTask(void* param) {
  char pendingCompartment[CALLBACK_ITEM_LEN];
  for (;;) {
    mailboxNetworkManager.update();

    if (httpCallbackQueue != nullptr && xQueueReceive(httpCallbackQueue, pendingCompartment, 0) == pdTRUE) {
      triggerCallback(pendingCompartment);
    }

    MailboxState localState = currentState;
    if ((localState == LOCKED || localState == MOTOR_ERROR) && !otaInProgress) {
      mqttManager.update();
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Network tasks don't need sub-ms timing
  }
}

void queueHttpCallback(const char* compartment) {
  if (!compartment || httpCallbackQueue == nullptr) {
    return;
  }
  char item[CALLBACK_ITEM_LEN];
  strncpy(item, compartment, sizeof(item) - 1);
  item[sizeof(item) - 1] = '\0';
  if (xQueueSend(httpCallbackQueue, item, 0) != pdTRUE) {
    Serial.println("Warning: HTTP callback queue full, dropping callback.");
  }
}

void triggerCallback(const char* compartment) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  Config config = configManager.getConfigCopy();
  if (config.callbackUrl.length() == 0) {
    return;
  }

  String url = config.callbackUrl;
  url.replace("{compartment}", compartment);
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  bool beginSuccess = false;

  if (url.startsWith("https://")) {
    // Keep caCert in the SAME scope as client and http so setCACert()'s raw pointer never dangles!
    String caCert = "";
    WiFiClientSecure client;
    if (config.callbackSkipCertVal) {
      Serial.println("HTTPS Callback: Skipping certificate validation (Insecure)");
      client.setInsecure();
    } else {
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
    if (beginSuccess) {
      int httpCode = http.GET();
      if (httpCode > 0) {
        telemetryLog.logf("HTTP", "HTTPS callback (%s) -> %d", compartment, httpCode);
      } else {
        telemetryLog.logf("HTTP", "HTTPS callback (%s) err: %s", compartment, http.errorToString(httpCode).c_str());
      }
      http.end();
    } else {
      telemetryLog.logf("HTTP", "HTTPS callback (%s) begin failed", compartment);
    }
  } else {
    WiFiClient client;
    beginSuccess = http.begin(client, url);
    if (beginSuccess) {
      int httpCode = http.GET();
      if (httpCode > 0) {
        telemetryLog.logf("HTTP", "HTTP callback (%s) -> %d", compartment, httpCode);
      } else {
        telemetryLog.logf("HTTP", "HTTP callback (%s) err: %s", compartment, http.errorToString(httpCode).c_str());
      }
      http.end();
    } else {
      telemetryLog.logf("HTTP", "HTTP callback (%s) begin failed", compartment);
    }
  }
}

bool canAcceptOpenRequest() {
  if (otaInProgress) {
    return false;
  }
  bool ok = false;
  portENTER_CRITICAL(&stateMux);
  ok = (currentState == LOCKED && (millis() - lockedStateEnterTime > REQUEST_COOLDOWN_MS));
  portEXIT_CRITICAL(&stateMux);
  return ok;
}

bool requestParcelOpening(const char* requester) {
  if (otaInProgress) {
    return false;
  }

  bool accepted = false;
  portENTER_CRITICAL(&stateMux);
  if (currentState == LOCKED && (millis() - lockedStateEnterTime > REQUEST_COOLDOWN_MS)) {
    preOpeningStateEnterTime = millis();
    closedAlreadyReachedInCycle = false;
    currentState = PRE_OPENING_TO_PARCEL;
    accepted = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (!accepted) {
    return false;
  }

  wiegandManager.detach();
  if (strcmp(requester, "webinterface") == 0 || strcmp(requester, "mqtt") == 0) {
    configManager.resetDeliveryBlockIfNeeded(requester);
  }
  strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
  lastUsed[sizeof(lastUsed) - 1] = '\0';
  telemetryLog.logf("ACCESS", "OPEN_PARCEL by '%s'", requester);
  melodyPlayer.play(configManager.getConfigCopy().selectedMelody);
  queueHttpCallback("parcel");
  return true;
}

bool requestMailOpening(const char* requester) {
  if (otaInProgress) {
    return false;
  }

  bool accepted = false;
  portENTER_CRITICAL(&stateMux);
  if (currentState == LOCKED && (millis() - lockedStateEnterTime > REQUEST_COOLDOWN_MS)) {
    preOpeningStateEnterTime = millis();
    closedAlreadyReachedInCycle = false;
    currentState = PRE_OPENING_TO_MAIL;
    accepted = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (!accepted) {
    return false;
  }

  wiegandManager.detach();
  if (strcmp(requester, "webinterface") == 0 || strcmp(requester, "mqtt") == 0) {
    configManager.resetDeliveryBlockIfNeeded(requester);
  }
  strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
  lastUsed[sizeof(lastUsed) - 1] = '\0';
  telemetryLog.logf("ACCESS", "OPEN_MAIL by '%s'", requester);
  melodyPlayer.play(configManager.getConfigCopy().selectedMelody);
  queueHttpCallback("mail");
  return true;
}

bool requestLocking(const char* requester) {
  if (otaInProgress) {
    return false;
  }

  bool alreadyClosed = switchManager.isClosedPressed();
  bool accepted = false;
  portENTER_CRITICAL(&stateMux);
  const bool moving = (currentState == OPENING_TO_PARCEL || currentState == OPENING_TO_MAIL || currentState == LOCKING);
  if (!moving) {
    if (alreadyClosed) {
      currentState = LOCKED;
      lockedStateEnterTime = millis();
      closedAlreadyReachedInCycle = true;
    } else {
      closedAlreadyReachedInCycle = false;
      currentState = LOCKING;
    }
    accepted = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (!accepted) {
    return false;
  }

  wiegandManager.detach();
  strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
  lastUsed[sizeof(lastUsed) - 1] = '\0';
  telemetryLog.logf("MOTOR", "Manual LOCK by '%s' -> %s", requester, mailboxStateName(currentState));
  return true;
}

bool resetMotorError(const char* requester) {
  if (otaInProgress) {
    return false;
  }

  bool accepted = false;
  portENTER_CRITICAL(&stateMux);
  const bool moving = (currentState == OPENING_TO_PARCEL || currentState == OPENING_TO_MAIL || currentState == LOCKING);
  if (!moving) {
    currentState = LOCKED;
    lockedStateEnterTime = millis();
    // Mark cycle closed so autolock won't immediately jam back into the closed stop;
    // the user can then safely press "Open Parcel" to back away from a closed-side stop or "Lock" to home.
    closedAlreadyReachedInCycle = true;
    accepted = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (!accepted) {
    return false;
  }

  strncpy(lastUsed, requester, sizeof(lastUsed) - 1);
  lastUsed[sizeof(lastUsed) - 1] = '\0';
  telemetryLog.logf("MOTOR", "Error cleared by '%s' -> LOCKED", requester);
  return true;
}

void receivedWiegandCode(char* code, uint8_t bits) {
  strncpy(lastScannedWiegandId, code, sizeof(lastScannedWiegandId) - 1);
  lastScannedWiegandId[sizeof(lastScannedWiegandId) - 1] = '\0';
  if (bits == 4 || bits == 8) {
    strncpy(lastKeypadCode, code, sizeof(lastKeypadCode) - 1);
    lastKeypadCode[sizeof(lastKeypadCode) - 1] = '\0';
  }

  // Check state AND 2.5s cooldown BEFORE redeeming one-time codes or enabling deliveryBlocked!
  if (!canAcceptOpenRequest()) {
    telemetryLog.logf("ACCESS", "Scan ignored (state=%s or 2.5s cooldown)", mailboxStateName(currentState));
    return;
  }

  std::string labelOut;
  Config config = configManager.getConfigCopy();
  AccessType result = AccessControl::evaluate(code, config.ownerCodes.c_str(), config.deliveryCodes.c_str(), &labelOut, bits);
  
  if (result == AccessType::OPEN_MAIL) {
    if (requestMailOpening(labelOut.c_str())) {
      if (deliveryBlocked) {
        configManager.saveDeliveryBlocked(false);
        telemetryLog.logf("ACCESS", "Delivery block reset by owner '%s'", labelOut.c_str());
      }
    }
    return;
  }
  
  // Check one-time codes next
  String otcLabel;
  if (configManager.checkAndRedeemOneTimeCode(code, otcLabel, bits)) {
    if (requestParcelOpening(otcLabel.c_str())) {
      if (config.oneTimeOpening) {
        configManager.saveDeliveryBlocked(true);
        telemetryLog.logf("ACCESS", "Delivery blocked after one-time code '%s'", otcLabel.c_str());
      }
    }
    return;
  }
  
  // Check regular delivery codes
  if (result == AccessType::OPEN_PARCEL) {
    if (config.oneTimeOpening && deliveryBlocked) {
      telemetryLog.logf("ACCESS", "DENIED delivery '%s' (one-time opening blocked)", labelOut.c_str());
      return;
    }
    if (requestParcelOpening(labelOut.c_str())) {
      if (config.oneTimeOpening) {
        configManager.saveDeliveryBlocked(true);
        telemetryLog.logf("ACCESS", "Delivery blocked after parcel code '%s'", labelOut.c_str());
      }
    }
    return;
  }

  telemetryLog.logf("ACCESS", "DENIED unknown code (bits=%u)", bits);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  telemetryLog.logf("MQTT", "RX %s: %s", topic, message.c_str());

  if (String(topic) == "paketkasten/command") {
    if (message == "OPEN_PARCEL") {
      requestParcelOpening("mqtt");
    } else if (message == "OPEN_MAIL") {
      requestMailOpening("mqtt");
    } else if (message == "LOCK") {
      requestLocking("mqtt");
    } else if (message == "RESET_ERROR") {
      resetMotorError("mqtt");
    }
  }
}

void publishState() {
  if (!configManager.hasMqttServer()) {
    return; // Avoid unbounded queue growth when MQTT is not configured
  }
  JsonDocument doc;
  doc["state"] = getMailboxStateString();
  doc["last_used"] = lastUsed;
  String output;
  serializeJson(doc, output);
  Serial.print("Queuing state for MQTT: ");
  Serial.println(output);
  if (xSemaphoreTake(mqttQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (mqttMessageQueue.size() >= MAX_MQTT_QUEUE_SIZE) {
      mqttMessageQueue.erase(mqttMessageQueue.begin());
    }
    mqttMessageQueue.push_back(output);
    xSemaphoreGive(mqttQueueMutex);
  }
}
