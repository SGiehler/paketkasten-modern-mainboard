#ifndef TELEMETRY_LOG_H
#define TELEMETRY_LOG_H

#include <Arduino.h>
#include <cstdint>

struct TelemetryEntry {
    uint32_t seq;
    unsigned long uptimeMs;
    char category[10];
    char message[104];
};

class TelemetryLog {
public:
    static constexpr size_t CAPACITY = 48;

    TelemetryLog();

    // Thread-safe (Core 0 / Core 1). Never call from inside an ISR.
    void logf(const char* category, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void clear();

    // Copies entries with seq > sinceSeq into outBuffer (up to maxCount). Returns number copied.
    size_t copySince(uint32_t sinceSeq, TelemetryEntry* outBuffer, size_t maxCount, uint32_t* latestSeqOut = nullptr);

private:
    TelemetryEntry _ring[CAPACITY];
    size_t _head;
    size_t _count;
    uint32_t _nextSeq;
    portMUX_TYPE _mux;
};

extern TelemetryLog telemetryLog;

#endif
