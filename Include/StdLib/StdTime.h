#ifndef VANARIZE_STDLIB_TIME_H
#define VANARIZE_STDLIB_TIME_H

#include <stdint.h>
#include "Core/VanarizeValue.h"

// Get current time in nanoseconds (monotonic clock)
Value StdTime_Now(void);

// Sleep for specified milliseconds
void StdTime_Sleep(uint64_t ms);

// Start/stop measurement timer
// First call starts timer, second call returns elapsed nanoseconds
#endif // VANARIZE_STDLIB_TIME_H
