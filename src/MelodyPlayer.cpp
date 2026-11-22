#include "MelodyPlayer.h"
#include "melodies.h"
#include "state.h"
#include <cstring>

// Dedicated High-Speed LEDC Channel 0 (Timer 0) for the buzzer, completely isolated
// from MotorController on Channels 2 & 3 (Timer 1). Driving LEDC directly avoids
// Arduino's blocking toneTask queue (~6 KB RAM saved) and gives exact 1 ms articulation.
static constexpr uint8_t BUZZER_LEDC_CHANNEL = 0;

MelodyPlayer melodyPlayer(BUZZER_PIN);

static int calcNoteDuration(int wholenote, int8_t divider) {
    if (divider > 0) {
        return wholenote / divider;
    }
    if (divider < 0) {
        int base = wholenote / (-divider);
        return base + base / 2; // dotted note = 1.5x
    }
    return 0;
}

MelodyPlayer::MelodyPlayer(int buzzerPin) :
    _buzzerPin(buzzerPin),
    _currentMelody(nullptr),
    _melodySize(0),
    _currentNoteIndex(0),
    _noteStartTime(0),
    _noteDuration(0),
    _toneDuration(0),
    _toneActive(false),
    _melodyPlaying(false),
    _wholenote(1200),
    _mux(portMUX_INITIALIZER_UNLOCKED),
    _hasPendingMelody(false)
{
    _pendingMelody[0] = '\0';
}

void MelodyPlayer::begin() {
    pinMode(_buzzerPin, OUTPUT);
    digitalWrite(_buzzerPin, LOW);
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 10);
    ledcAttachPin(_buzzerPin, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

// Thread-safe & non-blocking: can be called safely from AsyncWebServer (Core 0),
// mqttTask (Core 0), or WiegandTask (Core 1). All LEDC writes execute strictly on
// Core 1 inside MelodyPlayer::update().
void MelodyPlayer::play(const String& melodyName) {
    portENTER_CRITICAL(&_mux);
    strncpy(_pendingMelody, melodyName.c_str(), sizeof(_pendingMelody) - 1);
    _pendingMelody[sizeof(_pendingMelody) - 1] = '\0';
    _hasPendingMelody = true;
    portEXIT_CRITICAL(&_mux);
}

void MelodyPlayer::stopTone() {
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
    _toneActive = false;
}

void MelodyPlayer::startCurrentNote() {
    const MelodyNote& note = _currentMelody[_currentNoteIndex];
    _noteStartTime = millis();
    _noteDuration = calcNoteDuration(_wholenote, note.divider);
    // Sound for 88% of the slot so repeated pitches articulate crisply without stretching BPM
    _toneDuration = (_noteDuration * 88) / 100;
    if (_toneDuration < 15) {
        _toneDuration = _noteDuration;
    }

    if (note.freq == REST || _noteDuration <= 0) {
        stopTone();
    } else {
        ledcWriteTone(BUZZER_LEDC_CHANNEL, note.freq);
        _toneActive = true;
    }
}

void MelodyPlayer::startMelody(const char* melodyName) {
    _currentMelody = nullptr;
    _melodySize = 0;
    _currentNoteIndex = 0;
    _noteStartTime = 0;
    _noteDuration = 0;
    _toneDuration = 0;
    _melodyPlaying = false;
    stopTone();

    if (!melodyName || strcmp(melodyName, "NONE") == 0) {
        return;
    }

    if (strcmp(melodyName, "NOKIA_TUNE") == 0) {
        _wholenote = 1400; // Gran Vals (~171 BPM)
        static const MelodyNote nokiaTuneMelody[] = {
            {NOTE_E5, 8},  {NOTE_D5, 8},  {NOTE_FS4, 4}, {NOTE_GS4, 4},
            {NOTE_CS5, 8}, {NOTE_B4, 8},  {NOTE_D4, 4},  {NOTE_E4, 4},
            {NOTE_B4, 8},  {NOTE_A4, 8},  {NOTE_CS4, 4}, {NOTE_E4, 4},
            {NOTE_A4, 2}
        };
        _currentMelody = nokiaTuneMelody;
        _melodySize = sizeof(nokiaTuneMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "IMPERIAL_MARCH") == 0) {
        _wholenote = 2222; // ~108 BPM in A minor (fixed GS4 in bar 4 + true dotted-8th/16th march)
        static const MelodyNote imperialMarchMelody[] = {
            {NOTE_A4, 4},  {NOTE_A4, 4},  {NOTE_A4, 4},  {NOTE_F4, -8}, {NOTE_C5, 16},
            {NOTE_A4, 4},  {NOTE_F4, -8}, {NOTE_C5, 16}, {NOTE_A4, 2},
            {NOTE_E5, 4},  {NOTE_E5, 4},  {NOTE_E5, 4},  {NOTE_F5, -8}, {NOTE_C5, 16},
            {NOTE_GS4, 4}, {NOTE_F4, -8}, {NOTE_C5, 16}, {NOTE_A4, 2}
        };
        _currentMelody = imperialMarchMelody;
        _melodySize = sizeof(imperialMarchMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "MARIO") == 0) {
        _wholenote = 1200; // 200 BPM Overworld Theme (authentic 4/4 + quarter-note triplets)
        static const MelodyNote marioMelody[] = {
            {NOTE_E5, 8},  {NOTE_E5, 8},  {REST, 8},     {NOTE_E5, 8},  {REST, 8},     {NOTE_C5, 8},  {NOTE_E5, 4},
            {NOTE_G5, 4},  {REST, 4},     {NOTE_G4, 4},  {REST, 4},
            {NOTE_C5, -4}, {NOTE_G4, 8},  {REST, 4},     {NOTE_E4, -4},
            {NOTE_A4, 4},  {NOTE_B4, 4},  {NOTE_AS4, 8}, {NOTE_A4, 4},
            {NOTE_G4, 6},  {NOTE_E5, 6},  {NOTE_G5, 6},  {NOTE_A5, 4},  {NOTE_F5, 8},  {NOTE_G5, 8},
            {REST, 8},     {NOTE_E5, 4},  {NOTE_C5, 8},  {NOTE_D5, 8},  {NOTE_B4, -4}
        };
        _currentMelody = marioMelody;
        _melodySize = sizeof(marioMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "WINDOWS_XP_STARTUP") == 0) {
        _wholenote = 1600; // Authentic Eb major 6-note startup chime
        static const MelodyNote windowsXpStartupMelody[] = {
            {NOTE_DS6, 8}, {NOTE_DS5, 8}, {NOTE_AS5, -4}, {NOTE_GS5, 2}, {NOTE_DS6, 4}, {NOTE_AS5, 2}
        };
        _currentMelody = windowsXpStartupMelody;
        _melodySize = sizeof(windowsXpStartupMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "INTEL_INSIDE") == 0) {
        _wholenote = 1300; // G major (piezo sweet spot): intro hit + signature 4-note bong
        static const MelodyNote intelInsideMelody[] = {
            {NOTE_G4, 8}, {REST, 16}, {NOTE_G5, 8}, {NOTE_C6, 8}, {NOTE_G5, 8}, {NOTE_D6, 2}
        };
        _currentMelody = intelInsideMelody;
        _melodySize = sizeof(intelInsideMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "TETRIS") == 0) {
        _wholenote = 1600; // 150 BPM Korobeiniki (Game Boy Theme A, exact 4/4 & dotted rhythms)
        static const MelodyNote tetrisMelody[] = {
            // Bar 1
            {NOTE_E5, 4},  {NOTE_B4, 8},  {NOTE_C5, 8},  {NOTE_D5, 4},  {NOTE_C5, 8},  {NOTE_B4, 8},
            // Bar 2
            {NOTE_A4, 4},  {NOTE_A4, 8},  {NOTE_C5, 8},  {NOTE_E5, 4},  {NOTE_D5, 8},  {NOTE_C5, 8},
            // Bar 3
            {NOTE_B4, -4}, {NOTE_C5, 8},  {NOTE_D5, 4},  {NOTE_E5, 4},
            // Bar 4
            {NOTE_C5, 4},  {NOTE_A4, 4},  {NOTE_A4, 4},  {REST, 4},
            // Bar 5
            {REST, 8},     {NOTE_D5, 4},  {NOTE_F5, 8},  {NOTE_A5, 4},  {NOTE_G5, 8},  {NOTE_F5, 8},
            // Bar 6
            {NOTE_E5, -4}, {NOTE_C5, 8},  {NOTE_E5, 4},  {NOTE_D5, 8},  {NOTE_C5, 8},
            // Bar 7
            {NOTE_B4, 4},  {NOTE_B4, 8},  {NOTE_C5, 8},  {NOTE_D5, 4},  {NOTE_E5, 4},
            // Bar 8
            {NOTE_C5, 4},  {NOTE_A4, 4},  {NOTE_A4, 2}
        };
        _currentMelody = tetrisMelody;
        _melodySize = sizeof(tetrisMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "GEMINI") == 0) {
        _wholenote = 1440;
        static const MelodyNote geminiMelody[] = {
            {NOTE_A5, 8}, {NOTE_B5, 8}, {NOTE_CS6, 8}, {NOTE_E6, 4},
            {NOTE_D6, 8}, {NOTE_E6, 8}, {NOTE_FS6, 8}, {NOTE_A6, 2}
        };
        _currentMelody = geminiMelody;
        _melodySize = sizeof(geminiMelody) / sizeof(MelodyNote);
    } else if (strcmp(melodyName, "NEON") == 0) {
        _wholenote = 1400; // Custom Synthwave / Outrun Neon unlock jingle (~2.4s, Em9 -> Cmaj7 -> Em)
        static const MelodyNote neonMelody[] = {
            // Shimmering Em7 -> Em9 arpeggio
            {NOTE_E5, 16}, {NOTE_G5, 16}, {NOTE_B5, 16}, {NOTE_E6, 16},
            {NOTE_FS6, 8}, {NOTE_E6, 16}, {NOTE_D6, 16},
            // Cmaj7 -> D lift
            {NOTE_C6, 16}, {NOTE_E6, 16}, {NOTE_G6, 8},
            {NOTE_FS6, 8}, {NOTE_D6, 16}, {NOTE_B5, 16},
            // Bright cyber-chime unlock flourish
            {NOTE_B5, 16}, {NOTE_E6, 16}, {NOTE_G6, 16}, {NOTE_B6, 16},
            {NOTE_G6, 8},  {NOTE_E6, -4}
        };
        _currentMelody = neonMelody;
        _melodySize = sizeof(neonMelody) / sizeof(MelodyNote);
    } else {
        _wholenote = 1400;
        static const MelodyNote fallbackMelody[] = {
            {NOTE_E5, 8}, {NOTE_G5, 8}, {NOTE_B5, 4}
        };
        _currentMelody = fallbackMelody;
        _melodySize = sizeof(fallbackMelody) / sizeof(MelodyNote);
    }

    if (_currentMelody == nullptr || _melodySize == 0) {
        return;
    }

    _melodyPlaying = true;
    _currentNoteIndex = 0;
    startCurrentNote();
}

void MelodyPlayer::update() {
    if (_hasPendingMelody) {
        char requested[sizeof(_pendingMelody)] = "";
        portENTER_CRITICAL(&_mux);
        strncpy(requested, _pendingMelody, sizeof(requested) - 1);
        requested[sizeof(requested) - 1] = '\0';
        _hasPendingMelody = false;
        portEXIT_CRITICAL(&_mux);
        startMelody(requested);
    }

    if (!_melodyPlaying) {
        return;
    }

    unsigned long elapsed = millis() - _noteStartTime;
    if (_toneActive && elapsed >= (unsigned long)_toneDuration) {
        stopTone();
    }

    if (elapsed >= (unsigned long)_noteDuration) {
        _currentNoteIndex++;
        if (_currentNoteIndex < _melodySize) {
            startCurrentNote();
        } else {
            _melodyPlaying = false;
            stopTone();
        }
    }
}
