#include "TelemetryLog.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

TelemetryLog telemetryLog;

TelemetryLog::TelemetryLog() :
    _head(0),
    _count(0),
    _nextSeq(1),
    _mux(portMUX_INITIALIZER_UNLOCKED)
{
    memset(_ring, 0, sizeof(_ring));
}

void TelemetryLog::logf(const char* category, const char* fmt, ...) {
    char msgBuf[sizeof(TelemetryEntry::message)];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    const char* cat = (category && category[0] != '\0') ? category : "SYS";
    unsigned long now = millis();

    portENTER_CRITICAL(&_mux);
    TelemetryEntry& slot = _ring[_head];
    slot.seq = _nextSeq++;
    slot.uptimeMs = now;
    strncpy(slot.category, cat, sizeof(slot.category) - 1);
    slot.category[sizeof(slot.category) - 1] = '\0';
    strncpy(slot.message, msgBuf, sizeof(slot.message) - 1);
    slot.message[sizeof(slot.message) - 1] = '\0';

    _head = (_head + 1) % CAPACITY;
    if (_count < CAPACITY) {
        _count++;
    }
    portEXIT_CRITICAL(&_mux);

    Serial.printf("[%lu][%s] %s\n", now, cat, msgBuf);
}

void TelemetryLog::clear() {
    portENTER_CRITICAL(&_mux);
    _head = 0;
    _count = 0;
    portEXIT_CRITICAL(&_mux);
}

size_t TelemetryLog::copySince(uint32_t sinceSeq, TelemetryEntry* outBuffer, size_t maxCount, uint32_t* latestSeqOut) {
    size_t copied = 0;
    portENTER_CRITICAL(&_mux);
    if (latestSeqOut) {
        *latestSeqOut = (_nextSeq > 1) ? (_nextSeq - 1) : 0;
    }
    if (outBuffer && maxCount > 0 && _count > 0) {
        size_t start = (_head + CAPACITY - _count) % CAPACITY;
        for (size_t i = 0; i < _count && copied < maxCount; ++i) {
            const TelemetryEntry& e = _ring[(start + i) % CAPACITY];
            if (e.seq > sinceSeq) {
                outBuffer[copied++] = e;
            }
        }
    }
    portEXIT_CRITICAL(&_mux);
    return copied;
}
