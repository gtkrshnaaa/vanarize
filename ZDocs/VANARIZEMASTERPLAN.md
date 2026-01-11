# THE VANARIZE MASTERPLAN

**Codename:** Project V
**Target Architecture:** x86-64 (AMD64)
**Performance Objective:** 1.2 Billion Operations/Second (Throughput)
**Implementation Standard:** ISO C (C99/C11) Native — Zero Dependency Imperative.

---

## 1. Abstract & Core Philosophy

Vanarize constitutes a paradigm shift in high-performance computing, architected as a statically-typed, procedural language that leverages a **Direct-to-Machine-Code JIT (Just-In-Time)** compilation strategy. Diverging from traditional bytecode virtual machines or tree-walking interpreters, Vanarize eliminates the "fetch-decode-execute" dispatch overhead by emitting optimized x86-64 machine instructions directly into executable memory during the parsing phase.

The architecture is governed by the **"Zero-Dependency Imperative,"** necessitating a self-contained compiler infrastructure that relies exclusively on the host operating system's system calls (syscalls) and the standard C library, ensuring maximum portability and minimal footprint.

### 1.1 The Fundamental Axioms

1. **Direct Execution Paradigm:** The compilation pipeline operates as a strictly linear transformation: *Source Code*  *Abstract Syntax Tree (AST)*  *x64 Machine Code*. Intermediate representations (bytecode) are strictly prohibited to minimize latency.
2. **Type Rigidity:** Type resolution is deterministic at compile-time. All primitives and complex structures must be explicitly typed, eliminating runtime type inference and enabling the generation of branch-free arithmetic logic.
3. **Syntactic Purity:** Identifier nomenclature is strictly enforced to maximize code maintainability. The underscore character (`_`) is prohibited; identifiers must adhere to **PascalCase** (types/functions) or **camelCase** (variables).
4. **Implicit Modularity:** The namespace hierarchy is intrinsically mapped to the physical file system structure, enforcing a logical organization of code units.
5. **Native Asynchrony:** Concurrency is architected around a non-blocking Event Loop utilizing OS-native notification mechanisms (`epoll` on Linux, `kqueue` on BSD/macOS), managed natively via `async` and `await` keywords without thread-blocking overhead.

---

## 2. Runtime Architecture & Memory Model

### 2.1 The Unified 64-bit Value (NaN Boxing)

To optimize data locality and register utilization, Vanarize employs **NaN Boxing** (IEEE 754) for polymorphic storage in the heap (e.g., within Structs or Arrays). All values are encapsulated within a 64-bit unsigned integer (`uint64_t`).

* **Floating Point (`double`, `float`):** Conform strictly to the IEEE 754 standard.
* **Pointers (`string`, `struct`):** Encoded within the 48-bit significand of a Signaling NaN.
* **Booleans & Null:** Encoded using specific bit-mask patterns within the high 16 bits (Tagging).

> **Critical Performance Note:** While NaN Boxing is used for storage, the JIT compiler must perform **Register Promotion**. When integers (`int`, `long`) are loaded into CPU registers for computation, they must be "unboxed" into raw 64-bit integers to utilize single-cycle ALU instructions (`ADD`, `SUB`, `CMP`), bypassing floating-point units.

### 2.2 Memory Management & Garbage Collection

Vanarize implements a high-throughput **Precise, Stop-the-World, Mark-and-Sweep Garbage Collector**.

* **Allocation Strategy:**
* **The Nursery (Young Gen):** Utilizes a **Bump-Pointer Allocator** for rapid object creation ( complexity).
* **The Old Generation:** Objects surviving multiple collection cycles are promoted to a Free-List allocator.


* **Write Barriers:** The JIT emits inline write barriers to maintain heap integrity by tracking references migrating from the Old Generation to the Young Generation.

---

## 3. Compilation Pipeline

The compiler operates as a monolithic, single-pass engine designed for millisecond-level startup latency.

### 3.1 Lexical Analysis (Zero-Copy)

The Lexer executes a zero-copy tokenization process. It produces a stream of `Token` structures that reference the original source buffer pointers, thereby eliminating dynamic memory allocation (`malloc`) overhead for string literals during the analysis phase.

### 3.2 Abstract Syntax Tree (AST) Construction

The Parser utilizes a **Recursive Descent** algorithm with strict syntactic enforcement:

* **PascalCase:** Mandatory for `Function`, `Struct`, and `Namespace` identifiers.
* **camelCase:** Mandatory for `Variable` identifiers.
* **Underscore Rejection:** Immediate compilation failure upon detection of `_`.

### 3.3 The JIT Engine (Code Generation)

This component is the core of the Vanarize runtime, translating AST nodes directly into optimized x86-64 opcodes.

1. **Register Allocation (Linear Scan):**
* **Type Segregation:** Integer types (`int`, `long`, `boolean`) are mapped exclusively to General Purpose Registers (`RAX`, `RBX`, `RDI`, etc.). Floating-point types (`double`, `float`) are mapped to XMM registers (`XMM0`-`XMM15`).
* **Spilling:** Register pressure is managed by spilling excess variables to the native stack (`RSP`) only when physically necessary.


2. **Binary Emission:**
* Machine code is written to a contiguous memory block.
* **Memory Protection:** The block is finalized via `mmap` with `PROT_READ | PROT_WRITE | PROT_EXEC` permissions.


3. **Native ABI Compliance:**
* Adherence to the **System V AMD64 ABI** ensures seamless interoperability with the host C standard library (`libc`).



---

## 4. Language Specification

### 4.1 Primitive Types

Vanarize enforces a strict, statically-typed system. Implicit casting is prohibited to ensure arithmetic precision and performance predictability.

| Type | Bit-width | Description | Hardware Mapping |
| --- | --- | --- | --- |
| **`byte`** | 8 | Signed integer | GPR (AL/BL) |
| **`short`** | 16 | Signed integer | GPR (AX/BX) |
| **`int`** | 32 | Signed integer | GPR (EAX/EBX) |
| **`long`** | 64 | Signed integer | GPR (RAX/RBX) |
| **`float`** | 32 | Single-precision float | XMM (IEEE 754) |
| **`double`** | 64 | Double-precision float | XMM (IEEE 754) |
| **`char`** | 16 | Unicode character | GPR (AX) |
| **`boolean`** | 1 | Logical truth | GPR (AL) |
| **`string`** | 64 | Immutable String | GPR (Pointer) |

### 4.2 Syntax Rules

#### A. Variable Declaration & Type Safety

Variables must be declared with explicit primitive types to enable JIT optimization.

```vana
// Correct: Explicit Typing
int coreCount = 8
double clockSpeed = 3.5
string cpuName = "Intel Core i5-1135G7"
boolean isEnabled = true

// Concatenation
string status = "CPU: " + cpuName + " Speed: " + clockSpeed

```

#### B. Data Structures (`struct`)

Classes are rejected in favor of high-performance POD (Plain Old Data) Structs. Structs represent contiguous memory blocks.

```vana
// Struct: PascalCase required
struct NetworkConfig {
    string hostAddress
    int portId
    boolean isEncrypted
}

```

#### C. Control Structures

To guarantee predictable execution paths and facilitate loop unrolling optimizations, the `while` loop is **explicitly removed**. Iteration is exclusively governed by the canonical C-style `for` loop.

```vana
// Canonical For-Loop (Strict C-Style)
for (int i = 0; i < 1000; i++) {
    // Logic here
}

// Conditional Branching
if (i > 500) {
    // Logic here
} else if (i == 500) {
    // Logic here
} else {
    // Logic here
}

```

#### D. Functions

Functions require explicit type signatures using `::` for return types. PascalCase naming is mandatory.

```vana
// Function Definition
function CalculateVector(double x, double y) :: double {
    return x * y
}

// Void Function (No return type)
function PrintStatus(string msg) {
    print(msg)
}

```

#### E. Modularity (Imports)

The module system uses relative file paths resolved at compile-time. The filename acts as the Namespace identifier.

```vana
// Imports "Libs/MathUtils.vana" -> Namespace "MathUtils"
import "./Libs/MathUtils.vana"

function Main() {
    int res = MathUtils.Calculate(10)
}

```

---

## 5. Standard Library Specification

These libraries are implemented as compiler intrinsics, mapping directly to optimized C functions or assembly instructions.

### 5.1 `StdMath`

Maps directly to CPU AVX/SSE instructions (`ADDSD`, `SQRTSD`, etc.).

* **Functions:** `Sin`, `Cos`, `Tan`, `Sqrt`, `Pow`, `Abs`, `Floor`, `Ceil`.

### 5.2 `StdTime`

Provides high-resolution monotonic timing via `clock_gettime`.

* **Functions:** `Now()` (returns `long` nanoseconds), `Sleep(int ms)`, `Measure()`.

### 5.3 `StdNetwork`

Implements an event-driven I/O subsystem.

* **Architecture:** Non-blocking sockets managed by `epoll`/`kqueue`.
* **Functions:** `Listen`, `Accept`, `Get`, `Post`.

### 5.4 `StdJson`

A zero-copy JSON parser utilizing finite-state automata (FSA).

* **Functions:** `Parse`, `Stringify`, `GetValue`.

---

## 6. Implementation Strategy (Directory Structure)

The project adheres to a strict modular architecture separating Interface (`Include`) from Implementation (`Source`).

**Root: `/VanarizeProject**`

```text
VanarizeProject/
├── Build/                   # Compiled Artifacts
├── ZDocs/                   # Scientific Documentation
├── Include/                 # PUBLIC INTERFACES (.h)
│   ├── Core/                
│   │   ├── Memory.h         # Allocator & mmap Prototypes
│   │   ├── GarbageCollector.h
│   │   └── VanarizeValue.h  # NaN Boxing Definitions
│   ├── Compiler/            
│   │   ├── Token.h
│   │   ├── Lexer.h
│   │   └── Ast.h            
│   ├── Jit/                 # JIT CORE HEADERS
│   │   ├── CodeGen.h        
│   │   ├── AssemblerX64.h   # Opcode Definitions
│   │   └── RegisterMap.h    # Linear Scan Allocator
│   └── StdLib/              # STANDARD LIB HEADERS
│       ├── StdMath.h
│       ├── StdTime.h
│       ├── StdNetwork.h
│       └── StdJson.h
│
├── Source/                  # PRIVATE IMPLEMENTATION (.c)
│   ├── Core/
│   │   ├── Memory.c         # Bump Pointer Implementation
│   │   ├── GarbageCollector.c
│   │   └── VanarizeValue.c
│   ├── Compiler/
│   │   ├── Lexer.c          # Zero-Copy Tokenizer
│   │   ├── Parser.c         # Recursive Descent Logic
│   │   └── Ast.c
│   ├── Jit/
│   │   ├── CodeGen.c        # AST -> Assembly Translation
│   │   ├── AssemblerX64.c   # Hex Emitter (The "Metal" Layer)
│   │   ├── RegisterMap.c    # Register Allocation Logic
│   │   └── ExecutableMemory.c
│   └── StdLib/
│       ├── StdMath.c
│       ├── StdTime.c
│       ├── StdNetwork.c     # Raw Socket & Epoll Logic
│       └── StdJson.c        # FSM JSON Parser
│
├── Tests/                   # Unit Tests
│   └── TestAssembler.c      # Verification of Hex Codes
│
├── Examples/                # Reference Code
│   └── Main.vana
│
└── Makefile                 # Build Automation

```

---

## Appendix A: Reference Implementation

The following code demonstrates the syntactic strictness, explicit typing, and async capabilities of Vanarize. Note the exclusive use of `for` loops and C-style control flow.

**File:** `Main.vana`

```vana
// Struct Definition (PascalCase)
struct SensorData {
    string deviceId
    double temperature
    boolean isActive
}

// Helper Function (PascalCase)
function CalibrateSensor(double rawValue) :: double {
    // Native Math Usage
    return StdMath.Abs(rawValue * 0.98)
}

// Async Task Definition
async function UploadData(SensorData data) :: boolean {
    print("Uploading data for: " + data.deviceId)
    
    // JSON Serialization (Native)
    string payload = StdJson.Stringify(data)
    
    // Network Request (Native Async via Event Loop)
    string response = await StdNetwork.Post("https://api.vanarize.io/telemetry", payload)
    
    if (response == "200 OK") {
        return true
    } else {
        return false
    }
}

// Application Entry Point
async function Main() {
    print("Vanarize System Initializing...")
    
    // Explicit Variable Declaration (No 'var')
    int maxRetries = 3
    
    SensorData mainSensor = {
        deviceId: "Therm-X100",
        temperature: 42.5,
        isActive: true
    }

    // Control Flow (C-Style)
    if (mainSensor.isActive) {
        // Data Processing
        mainSensor.temperature = CalibrateSensor(mainSensor.temperature)
        
        // Loop Structure (Strict C-Style 'for', NO 'while')
        for (int i = 1; i <= maxRetries; i++) {
            print("Attempt " + i + "...")
            
            // Await Async Operation
            boolean success = await UploadData(mainSensor)
            
            if (success) {
                print("Upload Successful.")
                // Native Time Usage
                long latency = StdTime.Now() 
                print("Operation timestamp: " + latency + "ns")
                
                // Manual break logic via loop counter manipulation
                i = maxRetries + 1 
            } else {
                print("Upload Failed. Retrying...")
                await StdTime.Sleep(1000)
            }
        }
    } else {
        print("Sensor is inactive.")
    }
}

```

## ARTICLE 7: RUNTIME INVOCATION PROTOCOL (CLI)

The Vanarize Runtime Environment is invoked via the command-line interface.

### 7.1 Standard Execution

**Syntax:**

```bash
vanarize <filename>.vana

```

**Process Lifecycle:**

1. **Read:** The CLI loads `<filename>.vana` into memory.
2. **Compile:** The JIT Engine translates the entire transitive closure of imports into **x64 Machine Code** in RAM.
3. **Jump:** The CPU instruction pointer (`RIP`) is moved to the start of the `Main()` function.
4. **Event Loop:** The runtime enters the non-blocking event loop to handle `async` tasks.
5. **Terminate:** Upon completion of all tasks, the process exits.

### 7.2 Debugging

To verify the JIT output and ensure strict register usage (Integers in GPR, Floats in XMM), the compiler accepts a dump flag.

```bash
$ vanarize --dump-asm main.vana

[JIT DUMP] Function: CalculateVector
0x7F...00:  55          push   rbp
0x7F...01:  48 89 E5    mov    rbp, rsp
0x7F...04:  F2 0F 59 C1 mulsd  xmm0, xmm1  ; Explicit Floating Point Mul
0x7F...08:  5D          pop    rbp
0x7F...09:  C3          ret

```

---

**End of Specification.**
*Approved for Implementation.*