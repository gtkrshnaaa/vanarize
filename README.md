# Vanarize JIT Compiler

Vanarize is an ultra-high-performance, statically-typed scripting language that compiles directly to x86-64 machine code in memory. By producing native CPU instructions during parsing, Vanarize eliminates the overhead of intermediate representation and interpreter loops.

## Performance
Current Peak Throughput: 832,000,000 Operations/Second (Int Benchmark)

Vanarize achieves near-native performance through aggressive JIT optimizations:
- 128x Loop Unrolling: Dramatically reduces loop control overhead for high-frequency arithmetic.
- Register Promotion: Maps local variables directly to CPU registers (RBX, R12-R15).
- Inline Arithmetic: Emits single assembly instructions (ADD, SUB, IMUL) for numeric operations, bypassing runtime calls.
- Fused Compare-Branch: Compiles relational operators to direct CPU conditional jumps, skipping boolean object allocation.
- SIMD Infrastructure: Supports 256-bit AVX instructions (VPADDD, VADDPD) for vectorized throughput.

## Core Features
- Direct Machine Code Generation: No bytecode or VM interpretation.
- Strict Type System: Statically typed with explicit primitive widths.
- Async/Await: Built-in support for asynchronous programming with a native event loop.
- NaN-Boxing: Efficient 64-bit representation for all values (Doubles, Pointers, Booleans).
- Modules: Robust import system for code organization and reusable libraries.
- Structs: User-defined data types with direct memory mapping.
- Native Interop: Fully compliant with the System V AMD64 ABI.

## Type System
Vanarize implements a comprehensive primitive type system:
- Integers: byte (8), short (16), int (32), long (64).
- Floats: float (32), double (64).
- Text: char (16), string (managed).
- Logic: boolean (mapped to 1/0).

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
- Examples/: Test suite and benchmarks.

## Future Roadmap
- Reach and exceed 1.0B operations/second through further vectorization.
- Support for more complex struct layouts and method dispatch.
- Enhanced static analysis for even more aggressive fast-paths.