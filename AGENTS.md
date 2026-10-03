# Agent Guide & Documentation Indexes

Welcome! If you are a new AI coding agent assigned to work on the Paketkasten Revived codebase, this document is your entry point. It lists critical mechanical, electrical, and firmware architecture insights you must review before proposing any modifications.

## 🔑 Crucial Hardware & Firmware Documents

1. **[Motor Steering & Sensor Insights](doc/motor_steering_insights.md)**: 
   * **Location**: `doc/motor_steering_insights.md`
   * **Why you must read this**: This is the single most critical document detailing the Hall-effect sensor specifications (Honeywell SL353HT), high-frequency motor noise filtering, IRAM interrupt constraints (preventing Core 1 cache-miss crashes), and the zero-blanking-window edge detection logic. **Read this before modifying any logic in [SwitchManager.cpp](src/SwitchManager.cpp) or [main.cpp](src/main.cpp).**

2. **[Wiegand Hardware Guide](wiegand-hardware.md)**:
   * **Location**: `wiegand-hardware.md`
   * **Why you must read this**: Lists tested Wiegand-compatible keypad hardware (e.g. Gelikom BP3) and provides hardware integration tips (such as how to bypass the physical photosensor sabotage alarm).

## 🛠️ Main Source Files

* **State Management**: [state.h](include/state.h) / [state.cpp](src/state.cpp) - Houses state variables. *Note: Inter-task and ISR-shared variables are marked `volatile`.*
* **Limit Switch Control**: [SwitchManager.h](include/SwitchManager.h) / [SwitchManager.cpp](src/SwitchManager.cpp) - Configures switch pull-ups, manages IRAM-based interrupts, and performs high-speed digital debounce filtering.
* **Motor Control**: [MotorController.h](include/MotorController.h) / [MotorController.cpp](src/MotorController.cpp) - Applies the drive profile to the H-bridge pins (forward/reverse/braking), handles timeouts and logs every movement duration.
* **Motor Drive Profiles**: [MotorDriveProfile.h](include/MotorDriveProfile.h) - Pure, Arduino-free timing for the drive modes (Original / Slow-smooth / Slow-steps), stall escalation and timeouts. Unit-tested in [test/test_motor_profile](test/test_motor_profile/test_main.cpp).
* **Core Application Task & Cooldowns**: [main.cpp](src/main.cpp) - App task pinned to Core 1 running switch manager updates, motor controller ticks, autolock timing cooldowns, and Wiegand re-attachment delays to filter inductive motor spikes.

## ⚠️ Essential Development Commandments

* **Never call non-IRAM code in ISRs**: All functions called from `closedSwitchISR()`, `parcelSwitchISR()`, and `mailSwitchISR()` (such as pin-checking functions) **must** have the `IRAM_ATTR` attribute. Avoid standard ESP-IDF driver calls that reside in flash. Note: in this build `digitalRead()` is *not* IRAM-resident (`CONFIG_ARDUINO_ISR_IRAM` is off, so the GPIO ISRs are not IRAM-flagged either); see section H of the motor insights.
* **ISRs cannot brake the motor directly**: the motor pins are owned by LEDC, `digitalWrite()` on them has no effect. ISRs change the state and call `wakeAppTaskFromISR()`; `MotorController` brakes.
* **No Time-Based Startup Blanking Windows**: Because the motor runs extremely fast when warm/under full voltage, it can hit the parcel limit switch within $20\text{-}30\text{ms}$. Time-based masking windows will cause edge-trigger misses and catastrophic overshoot. Trust the $20\mu\text{s}$ digital filter inside the ISR instead.
* **Respect the Cooldown Timers**: The 1-second autolock state transition cooldown and 2.5-second request cooldown prevent state machine stutterstepping and power grid fluctuations under successive physical activations.
