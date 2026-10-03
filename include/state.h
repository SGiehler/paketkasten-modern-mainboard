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

// Constants (motor drive profile constants live in MotorDriveProfile.h)
extern const int PWM_FREQ;
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

// App task handle: limit switch ISRs notify it so the motor is braked immediately
// (the ISR itself cannot brake: the motor pins are owned by the LEDC peripheral).
extern TaskHandle_t appTaskHandle;

// Diagnostics of the last motor movement (start of movement until end/error state)
extern volatile unsigned long lastStrokeMs;
extern volatile MailboxState lastStrokeFrom;
extern volatile MailboxState lastStrokeTo;
extern volatile bool lastStrokeEscalated; // true: configured duty was too low, stall escalation kicked in

// Calibration variables
extern volatile bool calibrationActive;
extern volatile int calibrationStep;
extern volatile int calibrationCandidateDuty;
extern volatile unsigned long calibrationStepTime;
extern int calibratedOpen;
extern int calibratedClose;

extern std::vector<String> mqttMessageQueue;
extern SemaphoreHandle_t mqttQueueMutex;

// Global orchestrator functions
const char* mailboxStateName(MailboxState state);
String getMailboxStateString();
void requestParcelOpening(const char* requester);
void requestMailOpening(const char* requester);
void publishState();
bool startCalibration(); // false if the selected drive mode does not support calibration
void updateCalibration();

#endif


