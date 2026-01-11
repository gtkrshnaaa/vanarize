# Vanarize JIT Compiler

Vanarize is an ultra-high-performance, statically-typed scripting language that compiles directly to x86-64 machine code in memory. By producing native CPU instructions during parsing, Vanarize eliminates the overhead of intermediate representation and interpreter loops.

## Performance
Current Peak Throughput: 832,000,000 Operations/Second (Int Benchmark)

Vanarize achieves near-native performance through aggressive JIT optimizations:
- 128x Loop Unrolling: Dramatically reduces loop control overhead.
- Register Promotion: Maps local variables directly to CPU registers (RBX, R12-R15).
- Inline Arithmetic: Emits single assembly instructions for numeric operations.
- SIMD Infrastructure: Supports 256-bit AVX instructions for vectorized throughput.

### Benchmark Metrics (100M iterations)
| Operation Type | Throughput (Ops/Sec) | Latency (ns/op) |
|----------------|----------------------|-----------------|
| Integer (32-bit)| 832,000,000          | 1.20 ns         |
| Pure Loop      | 735,000,000          | 1.36 ns         |
| Boolean Logic  | 678,000,000          | 1.47 ns         |
| Double (64-bit)| 319,000,000          | 3.13 ns         |

## Syntax Guide

Vanarize uses a strict, C-style syntax designed for predictability and performance.

### Variable Declarations
```java
int count = 100;
double price = 19.99;
boolean active = true;
string name = "Vanarize";
```

### Structs and Objects
```java
struct User {
    int id;
    string name;
}

fn main() {
    User u = User();
    u.id = 1;
    u.name = "Admin";
}
```

### Functions and Control Flow
```java
fn fib(int n) int {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

fn loopExample() {
    for (int i = 0; i < 1000; i = i + 1) {
        // High-performance JIT-compiled loop
    }
}
```

### Async/Await
```java
async fn fetchData(string url) string {
    // Native event-loop integration
    return await request(url);
}
```

## Core Features
- Direct Machine Code Generation: No bytecode or VM interpretation.
- Strict Type System: Statically typed with explicit primitive widths.
- Async/Await: Built-in support for asynchronous programming.
- NaN-Boxing: Efficient 64-bit representation for all values.
- Modules: Robust import system with namespace resolution.

## Building
Requirements: GCC and an x86-64 Linux environment.

```bash
make
```

## Usage
Run any .vana script using the vanarize binary:

```bash
./vanarize Examples/Benchmarks/IntBenchmark.vana
```

## Project Structure
- Source/Jit/: Core JIT engine and x64 Assembler.
- Source/Compiler/: Lexer and Recursive Descent Parser.
- Source/Core/: Runtime engine, NaN-Boxing, and Mark-and-Sweep GC.
- Source/StdLib/: Standard libraries (Benchmark, Time, Network, Json, Math).