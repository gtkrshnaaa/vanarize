# Vanarize JIT Compiler

Vanarize is an ultra-high-performance, statically-typed scripting language that compiles **Directly to x64 Machine Code** in memory. 

Unlike traditional bytecode VMs (like Python or Lua's standard interpreter), Vanarize produces native CPU instructions during parsing, eliminating the "fetch-decode-execute" interpreter loop entirely.

## Performance
**Current Benchmark (`FastBench.vana`): ~321,000,000 Operations/Second**

Vanarize achieves this speed through aggressive JIT optimizations implemented in `Source/Jit`:
- **Register Promotion**: Local variables are mapped directly to CPU registers (`RBX`, `R12`-`R15`).
- **Inline Arithmetic**: Numeric operations (e.g., `i + 1`) emit single assembly instructions (`ADDSD`), bypassing runtime function calls.
- **blind Fast-Paths**: Static analysis (`isGuaranteedNumber`) removes runtime type checks for typed variables.
- **Fused Compare-Branch**: Relational operators (`<`, `>`) compile to direct CPU flag jumps (`UCOMISD` + `Jcc`), skipping boolean object allocation.

## Key Features
- **True JIT Compilation**: No bytecode, no intermediate representation. Source -> x64 Machine Code.
- **NaN-Boxing**: Efficient 64-bit value representation (Doubles, Pointers, Booleans packed into IEEE 754 slots).
- **C-Style Syntax**: Familiar syntax with typed variables (`number`, `string`).
- **Structs**: User-defined data structures with direct memory access.
- **Garbage Collection**: Simple mark-and-sweep GC for managed memory.
- **Native Interop**: System V AMD64 ABI compliant function calls.

## Building
Requirements: `gcc` (x86_64 Linux).

```bash
make
```

## Usage
Run any `.vana` script:

```bash
./vanarize Examples/FastBench.vana
```

## Project Structure
- `Source/Jit/`: The core JIT engine (`CodeGen.c`, `AssemblerX64.c`).
- `Source/Compiler/`: Lexer and Recursive Descent Parser.
- `Source/Core/`: Runtime, Memory Management, GC.
- `Examples/`: Benchmark and test scripts.

## Latency Analysis
The engine is currently bound by the dependency chain latency of `double` (IEEE 754) arithmetic.
- **Loop overhead**: ~8 cycles/iteration.
- **Throughput Limit**: ~500M ops/sec (theoretical max).
To exceed this (reaching >1.0B ops/sec), future versions will implement Typed Integers to utilize lower-latency integer ALU instructions.