# VANARIZE JIT IMPROVEMENT PLAN

**Date**: 2026-01-11  
**Target**: 1.2 Billion Operations/Second  
**Compliance**: 100% VANARIZEMASTERPLAN.md  
**Scope**: World-Class General-Purpose Language

---

## 1. Current State Analysis

### 1.1 Performance Metrics

| Metric | Current | Target | Gap |
|--------|---------|--------|-----|
| Ops/sec | ~180M | 1.2B | 6.7x |
| Stability | ~40% tests pass | 100% | Critical |

### 1.2 Root Causes of Performance Loss

1. **Type Domain Mismatch**: Integer fast-path stores raw int64, but system expects NaN-boxed
2. **Excessive Conversions**: 4 XMM instructions per integer operation
3. **No Register Promotion**: All locals spill to stack
4. **Naive Code Generation**: No optimization passes

### 1.3 Root Causes of Crashes

1. **Inconsistent Value Representation**: Some paths emit raw int64, others NaN-boxed
2. **Division Overflow**: Large number multiplication without overflow checking
3. **Stack Corruption**: Complex expressions corrupt register state

---

## 2. Architecture Redesign

### 2.1 Unified Value System (MASTERPLAN §2.1)

All values MUST be NaN-boxed at function boundaries and storage:

```
┌─────────────────────────────────────────────────────────────────┐
│                    64-bit NaN Boxing Layout                      │
├─────────────────────────────────────────────────────────────────┤
│ Double:   [Standard IEEE 754 - if (v & QNAN) != QNAN]           │
│ Pointer:  [QNAN | 48-bit address]                               │
│ True:     [0x7FFC000000000003]                                  │
│ False:    [0x7FFC000000000002]                                  │
│ Nil:      [0x7FFC000000000001]                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Rule**: Internal optimizations may use raw values, but MUST box before:
- Function calls
- Variable stores  
- Return statements

### 2.2 Guaranteed Type Optimization

**Strategy**: Vanarize JIT leverages the strict Java type system. Each primitive has a dedicated machine-code path.

```
Expression: long i = i + 1

OPTIMIZED PATH (GPR-Only):
1. Load i from storage (Raw int64)
2. ADD RAX, 1            ; 64-bit Integer ALU (1 cycle)
3. Store result (Raw int64)

Expression: double d = d + 1.5

OPTIMIZED PATH (XMM-Only):
1. Load d from storage (NaN-boxed double)
2. MOVQ XMM0, RAX
3. MOVSD XMM1, [Literal_1.5]
4. ADDSD XMM0, XMM1      ; Float add (3-4 cycles)
5. MOVQ RAX, XMM0
6. Store result (NaN-boxed)
```

### 2.3 Register Allocation Strategy (MASTERPLAN §3.3.1)

**Linear Scan Algorithm**:
1. Compute live ranges for all variables
2. Allocate registers in order: RAX, RCX, RDX, RBX, RSI, RDI, R8-R15
3. Spill only when all registers exhausted

**Priority**:
- Highest: Loop counters (always in register)
- High: Frequently accessed variables
- Low: Single-use temporaries

---

## 3. Implementation Phases

### Phase 0: Stabilization (CRITICAL)

**Goal**: All Examples run without crash.

**Changes**:
1. Remove broken integer fast-path from NODE_BINARY_EXPR
2. Use consistent NaN-boxed doubles for ALL arithmetic
3. Fix Native_Print to handle edge cases
4. Verify all control flow constructs

**Verification**: 
```bash
./vanarize Examples/*.vana  # All must complete
```

---

### Phase 1: Java-Primitive Engine

**Goal**: Implement the full suite of 8 Java primitive types in the JIT.

**Implementation**:
1. Update Lexer/Parser to recognize all Java types (`byte`, `short`, `int`, `long`, `float`, `double`, `char`, `boolean`).
2. Implement type-specific ALU emission (e.g., `ADD` for `int`, `ADDSD` for `double`).
3. Enforce Java's type conversion and promotion rules.

**Benefit**: Predictable, world-class language core.

---

### Phase 2: Register Promotion

**Goal**: Keep hot variables in GPR instead of stack.

**Implementation**:
1. Classify variables by usage frequency
2. Allocate callee-saved registers (RBX, R12-R15) for persistent values
3. Use caller-saved (RAX, RCX, RDX) for temporaries

**Expected Gain**: 30-50% speedup for loop-heavy code

---

### Phase 3: Constant Folding & Expression Opt

**Goal**: Evaluate constant expressions at compile-time.

**Implementation**:
```c
if (isLiteral(left) && isLiteral(right)) {
    // Evaluate and emit folded result
    int64_t result = left_val OP right_val;
    Asm_Mov_Imm64(RAX, result);
}
```

**Expected Gain**: Zero runtime cost for constant expressions.

---

### Phase 4: GC Optimization (MASTERPLAN §2.2)

**Goal**: Minimize allocation latency.

**Bump Pointer Nursery**:
```c
typedef struct {
    uint8_t* start;
    uint8_t* current;
    uint8_t* end;
} Nursery;

void* NurseryAlloc(Nursery* n, size_t size) {
    if (n->current + size > n->end) {
        // Trigger minor GC
        MinorGC();
    }
    void* ptr = n->current;
    n->current += size;
    return ptr;
}
```

**Cost**: Single pointer increment = 1 instruction

---

### Phase 5: Loop Optimizations

**Goal**: Maximize throughput for tight loops.

**5.1 Fused Compare-Branch**:
```asm
; BEFORE: 
CMP RAX, RCX
SETL AL
MOVZX RAX, AL
TEST RAX, RAX
JZ end

; AFTER:
CMP RAX, RCX
JGE end
```

**5.2 Loop Counter in Register**:
```asm
; Loop: for (i = 0; i < N; i++)
MOV R12, 0          ; i = 0 (keep in R12 entire loop)
loop_start:
  CMP R12, R13      ; i < N (N in R13)
  JGE loop_end
  ; ... body ...
  INC R12           ; i++ (single instruction)
  JMP loop_start
loop_end:
```

**5.3 Strength Reduction**:
- Replace `i * 8` with `i << 3`
- Replace `i / 2` with `i >> 1` (for integers)

---

## 4. Verification Plan

### 4.1 Correctness Tests

| Test | Expected |
|------|----------|
| `DebugTest.vana` | prints 1 |
| `SmallLoop.vana` | prints 10 |
| `IntTest.vana` | prints 5, 8, 1, Done |
| `ForTest.vana` | prints Sum: 10 |
| `MathTest.vana` | StdMath results |
| `Functions.vana` | Function calls work |

### 4.2 Performance Benchmarks

| Benchmark | Phase 0 | Phase 5 Target |
|-----------|---------|----------------|
| Empty loop 10M | ~180M/s | 1.2B/s |
| Increment loop | ~150M/s | 1.0B/s |
| Mixed arithmetic | ~100M/s | 500M/s |

---

## 5. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Regression during optimization | Comprehensive test suite |
| Performance target not met | Profile-guided optimization |
| ABI incompatibility | Strict System V compliance |
| GC pauses | Incremental/generational GC |

---

## 6. Timeline

| Phase | Duration | Milestone |
|-------|----------|-----------|
| Phase 0 | Immediate | All tests pass |
| Phase 1 | 1 session | Stable arithmetic |
| Phase 2 | 1 session | Register promotion |
| Phase 3 | 1 session | Safe int optimization |
| Phase 4 | 1 session | Fast GC |
| Phase 5 | 1 session | 1.2B ops/sec |

---

**Document Status**: APPROVED FOR IMPLEMENTATION
