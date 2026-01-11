#define _POSIX_C_SOURCE 199309L
#include "StdLib/StdBenchmark.h"
#include <stdio.h>
#include <time.h>

static struct timespec g_benchStart;
static struct timespec g_benchEnd;

void StdBenchmark_Start(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_benchStart);
    printf("[StdBenchmark] Timer Started.\n");
}

void StdBenchmark_End(Value iterationsVal) {
    clock_gettime(CLOCK_MONOTONIC, &g_benchEnd);
    
    // Calculate elapsed ns
    // using timespec math to be safe
    long seconds = g_benchEnd.tv_sec - g_benchStart.tv_sec;
    long ns = g_benchEnd.tv_nsec - g_benchStart.tv_nsec;
    
    if (g_benchStart.tv_sec > g_benchEnd.tv_sec) {
        // Clock skew?
        seconds = 0; ns = 0;
    }
    
    double elapsedSec = (double)seconds + (double)ns / 1e9;
    
    // Get iterations from Value
    long long iterations = 0;
    if (IsNumber(iterationsVal)) {
        iterations = (long long)ValueToNumber(iterationsVal);
    } else {
        printf("[StdBenchmark] Error: Iterations must be a number.\n");
        return;
    }
    
    if (elapsedSec <= 0.0) {
        printf("[StdBenchmark] Elapsed time too small or zero.\n");
        return;
    }
    
    double opsPerSec = (double)iterations / elapsedSec;
    double mops = opsPerSec / 1000000.0;
    
    printf("[StdBenchmark] Result:\n");
    printf("  Iterations: %lld\n", iterations);
    printf("  Elapsed:    %.6f sec\n", elapsedSec);
    printf("  Ops/Sec:    %.0f\n", opsPerSec);
    printf("  MOps/Sec:   %.2f M\n", mops);
    printf("----------------------------------------\n");
}
