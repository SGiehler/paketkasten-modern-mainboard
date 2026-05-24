#include "MelodyPlayer.h"
#include "melodies.h"
#include "state.h"

MelodyPlayer melodyPlayer(BUZZER_PIN);

MelodyPlayer::MelodyPlayer(int buzzerPin) :
    _buzzerPin(buzzerPin),
    _currentMelody(nullptr),
    _currentTempo(nullptr),
    _melodySize(0),
    _currentNoteIndex(0),
    _noteStartTime(0),
    _noteDuration(0),
    _melodyPlaying(false),
    _wholenote(1000)
{}

void MelodyPlayer::begin() {
    pinMode(_buzzerPin, OUTPUT);
}

void MelodyPlayer::play(const String& melodyName) {
    _currentMelody = nullptr;
    _currentTempo = nullptr;
    _melodySize = 0;
    _currentNoteIndex = 0;
    _noteStartTime = 0;
    _noteDuration = 0;
    _melodyPlaying = false;

    if (melodyName == "NOKIA_TUNE") {
        _wholenote = 1333;
        static int nokiaTuneMelody[] = {
            NOTE_E5, NOTE_D5, NOTE_FS4, NOTE_GS4, NOTE_CS5, NOTE_B4, NOTE_D4, NOTE_E4, NOTE_B4, NOTE_A4, NOTE_CS4, NOTE_E4, NOTE_A4
        };
        static int nokiaTuneTempo[] = {
            8, 8, 4, 4, 8, 8, 4, 4, 8, 8, 4, 4, 2
        };
        _currentMelody = nokiaTuneMelody;
        _currentTempo = nokiaTuneTempo;
        _melodySize = sizeof(nokiaTuneMelody) / sizeof(int);
    } else if (melodyName == "IMPERIAL_MARCH") {
        _wholenote = 1800;
        static int imperialMarchMelody[] = {
            NOTE_A4, NOTE_A4, NOTE_A4, NOTE_F4, NOTE_C5,
            NOTE_A4, NOTE_F4, NOTE_C5, NOTE_A4,
            NOTE_E5, NOTE_E5, NOTE_E5, NOTE_F5, NOTE_C5,
            NOTE_G4, NOTE_F4, NOTE_C5, NOTE_A4
        };
        static int imperialMarchTempo[] = {
            4, 4, 4, 5, 16,
            4, 5, 16, 2,
            4, 4, 4, 5, 16,
            4, 5, 16, 2
        };
        _currentMelody = imperialMarchMelody;
        _currentTempo = imperialMarchTempo;
        _melodySize = sizeof(imperialMarchMelody) / sizeof(int);
    } else if (melodyName == "MARIO") {
        _wholenote = 1000;
        static int marioMelody[] = {
            NOTE_E5, NOTE_E5, REST, NOTE_E5, REST, NOTE_C5, NOTE_E5, REST,
            NOTE_G5, REST, REST,  REST, NOTE_G4, REST, REST, REST,
            NOTE_C5, REST, REST, NOTE_G4, REST, REST, NOTE_E4, REST,
            REST, NOTE_A4, REST, NOTE_B4, REST, NOTE_AS4, NOTE_A4, REST,
            NOTE_G4, NOTE_E5, NOTE_G5, NOTE_A5, REST, NOTE_F5, NOTE_G5,
            REST, NOTE_E5, REST, NOTE_C5, NOTE_D5, NOTE_B4, REST, REST
        };
        static int marioTempo[] = {
            8, 8, 8, 8, 8, 8, 8, 8,
            4, 8, 8, 8, 4, 8, 8, 8,
            4, 8, 8, 4, 8, 8, 4, 8,
            8, 4, 8, 4, 8, 8, 4, 8,
            8, 8, 8, 8, 8, 8, 8,
            8, 4, 8, 8, 8, 4, 8, 8
        };
        _currentMelody = marioMelody;
        _currentTempo = marioTempo;
        _melodySize = sizeof(marioMelody) / sizeof(int);
    } else if (melodyName == "WINDOWS_XP_STARTUP") {
        _wholenote = 1000;
        static int windowsXpStartupMelody[] = {
            NOTE_DS5, NOTE_GS4, NOTE_AS4, NOTE_DS5, NOTE_GS4, NOTE_AS4, NOTE_DS5
        };
        static int windowsXpStartupTempo[] = {
            4, 8, 8, 2, 8, 8, 4
        };
        _currentMelody = windowsXpStartupMelody;
        _currentTempo = windowsXpStartupTempo;
        _melodySize = sizeof(windowsXpStartupMelody) / sizeof(int);
    } else if (melodyName == "INTEL_INSIDE") {
        _wholenote = 400;
        static int intelInsideMelody[] = {
            NOTE_DS4, NOTE_DS4, NOTE_GS4, NOTE_DS4, NOTE_AS4
        };
        static int intelInsideTempo[] = {
            2, 2, 2, 2, 1
        };
        _currentMelody = intelInsideMelody;
        _currentTempo = intelInsideTempo;
        _melodySize = sizeof(intelInsideMelody) / sizeof(int);
    } else if (melodyName == "TETRIS") {
        _wholenote = 1000;
        static int tetrisMelody[] = {
            NOTE_E5, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4,
            NOTE_A4, NOTE_A4, NOTE_C5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4,
            NOTE_C5, NOTE_D5, NOTE_E5, NOTE_C5, NOTE_A4, NOTE_A4, REST,
            NOTE_D5, NOTE_F5, NOTE_A5, NOTE_G5, NOTE_F5, NOTE_E5,
            NOTE_C5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_E5,
            NOTE_C5, NOTE_A4, NOTE_A4
        };
        static int tetrisTempo[] = {
            4, 8, 8, 4, 8, 8, 4, 4,
            4, 8, 8, 4, 8, 8, 4,
            8, 8, 4, 4, 4, 4, 4,
            4, 8, 8, 4, 8, 8, 4,
            8, 8, 4, 8, 8, 4, 8, 8,
            4, 4, 4
        };
        _currentMelody = tetrisMelody;
        _currentTempo = tetrisTempo;
        _melodySize = sizeof(tetrisMelody) / sizeof(int);
    } else {
        _wholenote = 2000;
        static int defaultMelody[] = {
            NOTE_CS4, NOTE_D4, REST, NOTE_E4, NOTE_F4, REST, NOTE_CS4, NOTE_D4,
            NOTE_E4, NOTE_F4, NOTE_B3, NOTE_D4, NOTE_CS4, NOTE_A3, NOTE_G3, NOTE_D3, NOTE_E3, NOTE_F3
        };
        static int defaultTempo[] = {
            8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 4, 4, 4, 8, 8, 2
        };
        _currentMelody = defaultMelody;
        _currentTempo = defaultTempo;
        _melodySize = sizeof(defaultMelody) / sizeof(int);
    }

    if (_currentMelody == nullptr || _currentTempo == nullptr || _melodySize == 0) {
        Serial.println("Error: Melody not found or empty.");
        return;
    }
    _melodyPlaying = true;
    _currentNoteIndex = 0;
    _noteStartTime = millis();
    _noteDuration = _wholenote / _currentTempo[_currentNoteIndex];
    tone(_buzzerPin, _currentMelody[_currentNoteIndex], _noteDuration);
}

void MelodyPlayer::update() {
    if (!_melodyPlaying) {
        return;
    }

    if (millis() - _noteStartTime >= _noteDuration * 1.20) { // Pause between notes
        noTone(_buzzerPin);
        _currentNoteIndex++;
        if (_currentNoteIndex < _melodySize) {
            _noteStartTime = millis();
            _noteDuration = _wholenote / _currentTempo[_currentNoteIndex];
            tone(_buzzerPin, _currentMelody[_currentNoteIndex], _noteDuration);
        } else {
            _melodyPlaying = false;
            noTone(_buzzerPin);
        }
    }
}
