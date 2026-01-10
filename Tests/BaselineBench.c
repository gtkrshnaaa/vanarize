// Simple C-level benchmark to establish baseline
#include "StdLib/StdTime.h"
#include <stdio.h>

int main() {
    uint64_t iterations = 10000000;
    uint64_t result = 0;
    
    uint64_t start = StdTime_Now();
    
    for (uint64_t i = 0; i < iterations; i++) {
        result += i;
    }
    
    uint64_t elapsed = StdTime_Now() - start;
    
    double seconds = elapsed / 1e9;
    double opsPerSec = iterations / seconds;
    
    printf("C Baseline Arithmetic: %.2f M ops/sec\n", opsPerSec / 1e6);
    printf("Result: %llu (prevent optimization)\n", result);
    
    return 0;
}
