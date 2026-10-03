#include "state.h"
#include "ConfigManager.h"
#include "MotorDriveProfile.h"

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

const int PWM_FREQ = 1000;
const int OPENING_DELAY_MS = 700;
const char* SOFTAP_PASSWORD = "G67zC4OiB";

// Global variables
volatile MailboxState currentState = LOCKED;
volatile bool deliveryBlocked = false;
char lastScannedWiegandId[20] = "";
char lastKeypadCode[20] = "";
char lastUsed[50] = "unknown";
volatile unsigned long openStateEnterTime = 0;
unsigned long preOpeningStateEnterTime = 0;
volatile unsigned long motorStartTime = 0;
volatile unsigned long lockedStateEnterTime = 0;
volatile bool wiegandAttached = true;
volatile bool shouldRestart = false;

TaskHandle_t appTaskHandle = nullptr;

volatile unsigned long lastStrokeMs = 0;
volatile MailboxState lastStrokeFrom = LOCKED;
volatile MailboxState lastStrokeTo = LOCKED;
volatile bool lastStrokeEscalated = false;

// Calibration variables
volatile bool calibrationActive = false;
volatile int calibrationStep = 0;
volatile int calibrationCandidateDuty = 20;
volatile unsigned long calibrationStepTime = 0;
int calibratedOpen = 0;
int calibratedClose = 0;

std::vector<String> mqttMessageQueue;
SemaphoreHandle_t mqttQueueMutex = nullptr;

const char* mailboxStateName(MailboxState state) {
  switch (state) {
    case LOCKED: return "LOCKED";
    case PRE_OPENING_TO_PARCEL: return "PRE_OPENING_TO_PARCEL";
    case OPENING_TO_PARCEL: return "OPENING_TO_PARCEL";
    case PARCEL_OPEN: return "PARCEL_OPEN";
    case PRE_OPENING_TO_MAIL: return "PRE_OPENING_TO_MAIL";
    case OPENING_TO_MAIL: return "OPENING_TO_MAIL";
    case MAIL_OPEN: return "MAIL_OPEN";
    case LOCKING: return "LOCKING";
    case MOTOR_ERROR: return "MOTOR_ERROR";
    default: return "UNKNOWN";
  }
}

String getMailboxStateString() {
  return mailboxStateName(currentState);
}

bool startCalibration() {
    if (!MotorProfile::calibrationSupported(configManager.getConfig().motorMode)) {
        Serial.println("[Calibration] Not available in inching mode (slider sets the pause, not a duty).");
        return false;
    }
    calibrationActive = true;
    calibrationStep = 1; // start with prep closing
    calibratedOpen = 0;
    calibratedClose = 0;
    calibrationCandidateDuty = 20;
    calibrationStepTime = millis();
    if (currentState == MOTOR_ERROR) {
        currentState = LOCKED;
    }
    Serial.println("[Calibration] Auto-calibration started!");
    return true;
}

