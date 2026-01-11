#define _POSIX_C_SOURCE 199309L
#include "StdLib/StdTime.h"
#include "Core/VanarizeValue.h"
#include <time.h>
#include <stdint.h>
#include <stdio.h>

// Global timer state for Measure()
static uint64_t measureStart = 0;
static int measureActive = 0;

uint64_t StdTime_GetRaw(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        printf("clock_gettime failed!\n");
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Exposed to Vanarize -> Must return Value
Value StdTime_Now(void) {
    double ns = (double)StdTime_GetRaw();
    return NumberToValue(ns);
}

void StdTime_Sleep(uint64_t ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, NULL);
}

// Exposed to Vanarize -> Must return Value
Value StdTime_Measure(void) {
    if (!measureActive) {
        // Start timer
        measureStart = StdTime_GetRaw();
        measureActive = 1;
        return NumberToValue(0.0);
    } else {
        // Stop timer and return elapsed
        uint64_t current = StdTime_GetRaw();
        double elapsed = (double)(current - measureStart);
        measureActive = 0;
        return NumberToValue(elapsed);
    }
}
