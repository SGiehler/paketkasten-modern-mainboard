#ifndef MELODY_PLAYER_H
#define MELODY_PLAYER_H

#include <Arduino.h>
#include <cstdint>

struct MelodyNote {
    uint16_t freq;
    int8_t divider; // >0: normal (4=quarter, 8=eighth); <0: dotted (-4=dotted quarter, -8=dotted eighth)
};

class MelodyPlayer {
public:
    MelodyPlayer(int buzzerPin);
    void begin();
    void play(const String& melodyName);
    void update();
    bool isPlaying() const { return _melodyPlaying; }

private:
    void startMelody(const char* melodyName);
    void startCurrentNote();
    void stopTone();

    int _buzzerPin;
    const MelodyNote* _currentMelody;
    int _melodySize;
    int _currentNoteIndex;
    unsigned long _noteStartTime;
    int _noteDuration;
    int _toneDuration;
    bool _toneActive;
    volatile bool _melodyPlaying;
    int _wholenote;

    portMUX_TYPE _mux;
    char _pendingMelody[32];
    volatile bool _hasPendingMelody;
};

extern MelodyPlayer melodyPlayer;

#endif
