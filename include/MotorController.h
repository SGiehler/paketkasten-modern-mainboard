#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <Arduino.h>
#include "state.h"
#include "MotorDriveProfile.h"

class MotorController {
public:
    MotorController(int pin1, int pin2);
    void begin();
    void update();

private:
    static constexpr uint8_t MOTOR_LEDC_CH1 = 2; // High-Speed Timer 1 (avoids Channel 0 used by tone())
    static constexpr uint8_t MOTOR_LEDC_CH2 = 3;
    static constexpr uint8_t MOTOR_LEDC_RES = 8;

    int _pin1;
    int _pin2;
    int _lastIn1;
    int _lastIn2;
    MailboxState _lastMotorState;
    unsigned long _brakeStartTime;
    bool _brakeActive;
    bool _escalated;

    void writePins(int in1, int in2);
    void apply(const MotorOutput& out, bool opening);
    void brakeNow();
    void recordStroke(MailboxState from, MailboxState to);
};

extern MotorController motorController;

#endif
