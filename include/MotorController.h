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
    int _pin1;
    int _pin2;
    MailboxState _lastMotorState;
    bool _escalated;

    void apply(const MotorOutput& out, bool opening);
    void brakeNow();
    void recordStroke(MailboxState from, MailboxState to);
};

extern MotorController motorController;

#endif
