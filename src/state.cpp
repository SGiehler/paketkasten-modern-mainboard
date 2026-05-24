#include "state.h"

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

// Constants
const int PWM_FREQ = 1000;
const int FULL_POWER_MS = 100;
const int FULL_POWER_DUTY_CYCLE = 200;
const int RAMP_DOWN_MS = 10;
const int OPENING_DELAY_MS = 700;
const char* SOFTAP_PASSWORD = "G67zC4OiB";

// Global variables
volatile MailboxState currentState = LOCKED;
volatile bool deliveryBlocked = false;
char lastScannedWiegandId[20] = "";
char lastKeypadCode[20] = "";
char lastUsed[50] = "unknown";
unsigned long openStateEnterTime = 0;
unsigned long preOpeningStateEnterTime = 0;
unsigned long motorStartTime = 0;
unsigned long lockedStateEnterTime = 0;
bool wiegandAttached = true;
volatile bool shouldRestart = false;

std::vector<String> mqttMessageQueue;
SemaphoreHandle_t mqttQueueMutex = nullptr;

String getMailboxStateString() {
  switch (currentState) {
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
