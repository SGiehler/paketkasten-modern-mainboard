#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <vector>

enum MailboxState {
  LOCKED,
  PRE_OPENING_TO_PARCEL,
  OPENING_TO_PARCEL,
  PARCEL_OPEN,
  PRE_OPENING_TO_MAIL,
  OPENING_TO_MAIL,
  MAIL_OPEN,
  LOCKING,
  MOTOR_ERROR
};

// Pins (extern declarations or definitions, let's keep definitions in src/state.cpp or main.cpp. Let's declare them as extern here so all drivers can access them.)
extern const int MOTOR_PIN_1;
extern const int MOTOR_PIN_2;
extern const int GREEN_LED_PIN;
extern const int RED_LED_PIN;
extern const int CLOSED_SWITCH_PIN;
extern const int PARCEL_SWITCH_PIN;
extern const int MAIL_SWITCH_PIN;
extern const int WIEGAND_D0_PIN;
extern const int WIEGAND_D1_PIN;
extern const int BUZZER_PIN;

// Constants
extern const int PWM_FREQ;
extern const int FULL_POWER_MS;
extern const int FULL_POWER_DUTY_CYCLE;
extern const int RAMP_DOWN_MS;
extern const int OPENING_DELAY_MS;
extern const char* SOFTAP_PASSWORD;

// Global shared variables
extern volatile MailboxState currentState;
extern volatile bool deliveryBlocked;
extern char lastScannedWiegandId[20];
extern char lastKeypadCode[20];
extern char lastUsed[50];
extern volatile unsigned long openStateEnterTime;
extern unsigned long preOpeningStateEnterTime;
extern volatile unsigned long motorStartTime;
extern volatile unsigned long lockedStateEnterTime;
extern volatile bool wiegandAttached;
extern volatile bool shouldRestart;

extern std::vector<String> mqttMessageQueue;
extern SemaphoreHandle_t mqttQueueMutex;

// Global orchestrator functions
String getMailboxStateString();
void requestParcelOpening(const char* requester);
void requestMailOpening(const char* requester);
void publishState();

#endif

