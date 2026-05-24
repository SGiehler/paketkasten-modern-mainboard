#ifndef MELODY_PLAYER_H
#define MELODY_PLAYER_H

#include <Arduino.h>

class MelodyPlayer {
public:
    MelodyPlayer(int buzzerPin);
    void begin();
    void play(const String& melodyName);
    void update();
    bool isPlaying() const { return _melodyPlaying; }

private:
    int _buzzerPin;
    int* _currentMelody;
    int* _currentTempo;
    int _melodySize;
    int _currentNoteIndex;
    unsigned long _noteStartTime;
    int _noteDuration;
    bool _melodyPlaying;
    int _wholenote;
};

extern MelodyPlayer melodyPlayer;

#endif
