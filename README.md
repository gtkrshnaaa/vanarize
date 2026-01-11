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
Benchmarks were performed on an **Intel Core i5-1135G7 (11th Gen)** with **8GB RAM** running **Ubuntu Linux**.

| Operation Type | Throughput (Ops/Sec) | Latency (ns/op) |
|----------------|----------------------|-----------------|
| Integer (32-bit)| 832,000,000          | 1.20 ns         |
| Pure Loop      | 735,000,000          | 1.36 ns         |
| Boolean Logic  | 678,000,000          | 1.47 ns         |
| Double (64-bit)| 319,000,000          | 3.13 ns         |

## Syntax Guide

Vanarize uses a strict, procedural syntax defined in the Master Plan (Project V).

### Variable Declarations
Variables must use explicit primitive types and adhere to camelCase naming.
```java
int userCount = 1024;
double piValue = 3.14159;
boolean isActive = true;
string portalName = "Vanarize";
```

### Data Structures (Structs)
Structs are Plain Old Data (POD) containers and must use PascalCase.
```java
struct UserProfile {
    string name
    int age
    boolean active
}

function Main() {
    UserProfile admin = {
        name: "Alice",
        age: 30,
        active: true
    };
}
```

### Functions and Control Flow
Functions require PascalCase names and explicit return types using double-colon syntax.
```java
function Fibonacci(int n) :: int {
    if (n < 2) return n;
    return Fibonacci(n - 1) + Fibonacci(n - 2);
}

function IterationDemo() {
    // Standard C-style for-loop (while is prohibited)
    for (int i = 0; i < 1000; i++) {
        print("Iteration: " + i);
    }
}
```

### Async/Await
Async functions provide first-class support for the native event loop.
```java
async function FetchNetworkData(string targetUrl) :: string {
    // Non-blocking network I/O
    return await StdNetwork.Get(targetUrl);
}
```

## Core Features
- Direct Machine Code Generation: No bytecode or VM interpretation.
- Strict Type System: Statically typed with explicit primitive widths.
- Async/Await: Built-in support for asynchronous programming.
- NaN-Boxing: Efficient 64-bit representation for all objects.
- Modules: Filename-based namespace resolution via import.

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