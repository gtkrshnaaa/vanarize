# Vanarize Benchmark Suite

## Location
`Examples/Benchmarks/`

## Available Benchmarks

| Benchmark | Description | Iterations |
|-----------|-------------|------------|
| IntBenchmark.vana | 32-bit integer ops | 100M |
| LongBenchmark.vana | 64-bit integer ops | 100M |
| FloatBenchmark.vana | 32-bit float ops | 100M |
| DoubleBenchmark.vana | 64-bit double ops | 100M |
| BooleanBenchmark.vana | Boolean logic ops | 100M |
| LoopOnlyBenchmark.vana | Pure loop overhead | 100M |

## How to Run

```bash
time ./vanarize Examples/Benchmarks/IntBenchmark.vana
```

## Performance Targets

| Time | Throughput | Rating |
|------|------------|--------|
| < 0.10 sec | 1.0B+ ops/sec | EXCELLENT |
| < 0.33 sec | 300M+ ops/sec | GOOD |
| < 1.00 sec | 100M+ ops/sec | ACCEPTABLE |
| > 1.00 sec | < 100M ops/sec | NEEDS WORK |

## Known Issues

The following JIT bugs currently affect benchmark accuracy:

1. **Integer reassignment bug**: `x = x + 1` produces incorrect values
2. **Print after print**: Second print in sequence shows garbage
3. **Post-execution segfault**: Program crashes after Main() returns

These issues need to be resolved before accurate benchmarking is possible.
