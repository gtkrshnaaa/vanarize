# Vanarize JIT Performance Log
**Date:** 2026-01-11
**Benchmark:** `Examples/FastBench.vana` (100M Iterations)
**Target:** 1.2 Billion Ops/Sec

## Summary
During the "Performance Optimization" sprint, we achieved a **2.86x speedup**, increasing throughput from **112M** to **321M** operations per second.

The theoretical maximum for the current architecture (Double-Precision NaN Boxing) is estimated around **500M ops/sec** due to instruction latency chains. Reaching 1.2B requires a shift to native Integers.

## Benchmark History

| Stage | Speed (Ops/Sec) | Improvement | Description |
|-------|-----------------|-------------|-------------|
| **Baseline** | **112,000,000** | 1.0x | Initial JIT implementation (Stack-based). |
| **Register Promotion** | 109,000,000 | 0.97x | Moved locals to registers (`RBX`..`R15`). *Dip caused by fallback to C Runtime for arithmetic.* |
| **Inline Addition** | 308,000,000 | 2.75x | Replaced `Runtime_Add` with inline `ADDSD` assembly. Removed Type Checks for typed variables. |
| **Stackless Temp** | 311,000,000 | 2.77x | Used `R10` register for temporary operand storage, removing `PUSH/POP` in expression eval. |
| **Fused Branch** | **321,000,000** | **2.86x** | Fused `UCOMISD` (Compare) with `JAE` (Jump), removing boolean object creation in loop conditions. |

## Technical Bottleneck Analysis
The 1.2B target remains unreachable with the current `Value` type (IEEE 754 Double).
- **Latency Bound**: The loop `i = i + 1` depends on `ADDSD`, which has a latency of ~4 CPU cycles.
- **Cycle Count**: Total loop overhead is ~8-10 cycles per iteration.
- **Max Throughput**: At 4GHz, max throughput is ~400-500M ops/sec.
- **Solution**: Future work must implement **Integer Specialization** to use `ADD` (1 cycle latency) instead of `ADDSD`.
