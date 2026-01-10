#include "StdLib/StdTime.h"
#include <time.h>
#include <stdint.h>
#include <stdio.h>

// Global timer state for Measure()
static uint64_t measureStart = 0;
static int measureActive = 0;

uint64_t StdTime_Now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void StdTime_Sleep(uint64_t ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, NULL);
}

uint64_t StdTime_Measure(void) {
    if (!measureActive) {
        // Start timer
        measureStart = StdTime_Now();
        measureActive = 1;
        return 0;
    } else {
        // Stop timer and return elapsed
        uint64_t elapsed = StdTime_Now() - measureStart;
        measureActive = 0;
        return elapsed;
    }
}
