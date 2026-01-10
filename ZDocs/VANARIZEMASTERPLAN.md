# THE VANARIZE SPECIFICATION

**Codename:** Project V
**Target Architecture:** x86-64
**Performance Objective:** 1.2 Billion Operations/Second (Throughput)
**Implementation Standard:** ISO C (C99/C11) Native — No External Dependencies.

---

## 1. Abstract & Core Philosophy

Vanarize is designed as a high-performance, statically-typed, procedural language that utilizes a **Direct-to-Machine-Code JIT (Just-In-Time)** compilation strategy. Unlike traditional interpreters or bytecode VMs, Vanarize eliminates the "fetch-decode-execute" loop overhead by emitting executable machine instructions into memory immediately upon parsing.

The development of Vanarize adheres to the **"Zero-Dependency Imperative."** The compiler must be self-contained, relying solely on the host operating system's system calls (syscalls) and the standard C library.

### 1.1 The Fundamental Axioms

1. **Direct Execution Paradigm:** Source code  AST  x64 Machine Code. No intermediate bytecode representation is permitted.
2. **Type Rigidity:** Types are resolved at compile-time. The `number`, `text`, and `boolean` primitives are strict.
3. **Syntactic Purity:** The usage of the underscore character (`_`) is strictly prohibited in all identifiers to enforce readability.
4. **Implicit Modularity:** The file system hierarchy defines the namespace structure.
5. **Native Asynchrony:** Concurrency is handled via a native Event Loop (epoll/kqueue) integrated with the JIT runtime.

---

## 2. Runtime Architecture & Memory Model

### 2.1 The Unified 64-bit Value (NaN Boxing)

To maximize CPU register utilization, Vanarize employs **NaN Boxing** (IEEE 754). All variables, regardless of type, are passed as a 64-bit unsigned integer (`uint64_t`).

* **Double Precision Floats (`number`):** Occupy the full 64 bits.
* **Pointers (`text`, `struct`):** Stored within the 48-bit significand of a Signaling NaN.
* **Booleans & Null:** Encoded using specific bit-patterns within the high 16 bits (Tagging).

### 2.2 Memory Management & Garbage Collection

Vanarize utilizes a **Precise, Stop-the-World, Mark-and-Sweep Garbage Collector**.

* **Allocation Strategy:**
* **The Nursery (Young Gen):** A contiguous block of memory using a **Bump-Pointer Allocator**. Allocation is reduced to a single pointer increment instruction ( complexity).
* **The Old Generation:** When the Nursery fills, live objects are promoted to a Free-List based allocator.


* **Write Barriers:** The JIT emits write barriers to track pointers moving from Old Generation to Young Generation.

---

## 3. Compilation Pipeline

The compiler operates in a single linear pass to ensure millisecond-level startup times.

### 3.1 Lexical Analysis (Zero-Copy)

The Lexer performs zero-copy tokenization. It generates a stream of `Token` structures containing pointers to the original source buffer, avoiding dynamic memory allocation (`malloc`) for string literals during this phase.

### 3.2 Abstract Syntax Tree (AST) Construction

The Parser utilizes a **Recursive Descent** algorithm. It enforces:

* **PascalCase** for `Function`, `Struct`, and `Namespace` nodes.
* **camelCase** for `Variable` nodes.
* **Underscore Rejection:** Immediate syntax error upon detection of `_`.

### 3.3 The JIT Engine (Code Generation)

The core of Vanarize. It translates AST nodes directly into x64 opcodes.

1. **Register Allocation (Linear Scan):**
* The compiler maps variables to physical CPU registers (`RAX`, `RBX`, `RCX`, `RDI`, `RSI`, `R8`-`R15`).
* **Spilling:** If registers are exhausted, values are spilled to the native stack (`RSP`).


2. **Binary Emission:**
* Machine code is written to a memory buffer.
* **Memory Protection:** The buffer is marked executable via `mmap` (Linux/Unix) with flags `PROT_READ | PROT_WRITE | PROT_EXEC`.


3. **Native ABI Compliance:**
* Vanarize follows the **System V AMD64 ABI** calling convention, allowing seamless invocation of internal C functions (`StdLib`).



---

## 4. Language Specification

### 4.1 Primitive Types

| Type | Description | Internal Representation |
| --- | --- | --- |
| **`number`** | Numeric value (Integer/Float) | 64-bit IEEE 754 Double |
| **`text`** | Immutable String | Pointer to Heap-allocated UTF-8 |
| **`boolean`** | Logical Truth | Tagged Value (1 byte effective) |
| **`void`** | Empty Return | N/A |

### 4.2 Syntax Rules

#### A. Variable Declaration & String Concatenation

String concatenation uses the `+` operator.

```javascript
// Variable: camelCase
number coreCount = 8
text cpuName = "Intel Core i5-1153G7"

// Concatenation
text status = "CPU: " + cpuName + " has " + coreCount + " cores."

```

#### B. Data Structures (`struct`)

Classes are rejected in favor of POD (Plain Old Data) Structs.

```javascript
// Struct: PascalCase
struct NetworkConfig {
    text hostAddress
    number portId
    boolean isEncrypted
}

```

#### C. Control Structures

Loops adhere strictly to the C-style iteration paradigm.

```javascript
// Iteration
for (number i = 0; i < 1000; i++) {
    // Logic here
}

// Conditional Branching
if (i > 500) {
    // Logic here
} else {
    // Logic here
}

```

#### D. Functions

Declared with `function`, PascalCase naming, and `::` return type syntax.

```javascript
// Function Definition
function CalculateVector(number x, number y) :: number {
    return x * y
}

// Void Function
function PrintStatus(text msg) {
    print(msg)
}

```

#### E. Modularity (Imports)

Imports are relative. The filename becomes the Namespace identifier.

```javascript
// Imports "Libs/MathUtils.vana" as namespace "MathUtils"
import "./Libs/MathUtils.vana"

function Main() {
    number res = MathUtils.Calculate(10)
}

```

---

## 5. Standard Library Specification

These libraries are implemented in C and exposed as intrinsic namespaces.
No need to import them. Become available automatically. By design. By default.

### 5.1 `StdMath`

Provides direct mappings to CPU AVX/SSE instructions.

* **Functions:** `Sin`, `Cos`, `Tan`, `Sqrt`, `Pow`, `Abs`, `Floor`, `Ceil`.

### 5.2 `StdTime`

Provides high-resolution monotonic timing.

* **Implementation:** `clock_gettime(CLOCK_MONOTONIC)`.
* **Functions:** `Now()` (nanoseconds), `Sleep(ms)`, `Measure()`.

### 5.3 `StdNetwork`

Implements non-blocking I/O using BSD Sockets.

* **Architecture:** Event-driven using `epoll` (Linux) or `kqueue` (macOS).
* **Functions:** `Listen`, `Accept`, `Get` (HTTP), `Post` (HTTP).

### 5.4 `StdJson`

A finite-state-machine (FSM) based JSON parser optimized for zero-copy read operations.

* **Functions:** `Parse`, `Stringify`, `GetValue`.

---

## 6. Implementation Strategy (Directory Structure)

The project structure is strictly modular, enforcing a separation of Interface (`Include`) and Implementation (`Source`).

**Root: `/VanarizeProject**`

```text
VanarizeProject/
├── Build/                   # Compiled Artifacts
├── ZDocs/                   # Scientific Documentation
├── Include/                 # PUBLIC INTERFACES (.h)
│   ├── Core/                
│   │   ├── Memory.h         # Allocator & mmap Prototypes
│   │   ├── GarbageCollector.h
│   │   └── VanarizeValue.h    # NaN Boxing Definitions
│   ├── Compiler/            
│   │   ├── Token.h
│   │   ├── Lexer.h
│   │   └── Ast.h            
│   ├── Jit/                 # JIT CORE HEADERS
│   │   ├── CodeGen.h        
│   │   ├── AssemblerX64.h   # Opcode Definitions
│   │   └── RegisterMap.h    
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
│   │   ├── RegisterMap.c    # Linear Scan Allocator
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

The following code demonstrates the syntactical strictness and capabilities of Vanarize.

**File:** `Main.vana`

```javascript

// Struct Definition (PascalCase)
struct SensorData {
    text deviceId
    number temperature
    boolean isActive
}

// Helper Function (PascalCase)
function CalibrateSensor(number rawValue) :: number {
    // Native Math Usage
    return StdMath.Abs(rawValue * 0.98)
}

// Async Task Definition
async function UploadData(SensorData data) :: boolean {
    print("Uploading data for: " + data.deviceId)
    
    // JSON Serialization (Native)
    text payload = StdJson.Stringify(data)
    
    // Network Request (Native Async)
    // Simulating a POST request
    text response = await StdNetwork.Post("https://api.vanarize.io/telemetry", payload)
    
    if (response == "200 OK") {
        return true
    }
    return false
}

// Application Entry Point
async function Main() {
    print("Vanarize System Initializing...")
    
    // Variable Declaration (camelCase)
    number maxRetries = 3
    
    SensorData mainSensor = {
        deviceId: "Therm-X100",
        temperature: 42.5,
        isActive: true
    }

    // Control Flow
    if (mainSensor.isActive) {
        // Data Processing
        mainSensor.temperature = CalibrateSensor(mainSensor.temperature)
        
        // Loop Structure (C-Style)
        for (number i = 1; i <= maxRetries; i++) {
            print("Attempt " + i + "...")
            
            // Await Async Operation
            boolean success = await UploadData(mainSensor)
            
            if (success) {
                print("Upload Successful.")
                // Native Time Usage
                number latency = StdTime.Measure()
                print("Operation time: " + latency + "ns")
                
                // Break loop manual simulation (if break implemented) or logical exit
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

The Vanarize Runtime Environment is invoked via the command-line interface. While it functions internally as a complex Just-In-Time compiler, the user experience is designed to be as frictionless as a scripting interpreter.

### 7.1 Standard Execution

To execute a Vanarize program, the user invokes the `vanarize` binary followed by the entry file path.

**Syntax:**

```bash
vanarize <filename>.vana

```

**Process Lifecycle:**

1. **Read:** The CLI loads `<filename>.nara` into memory.
2. **Compile:** The JIT Engine translates the entire transitive closure of imports into **x64 Machine Code** in RAM.
3. **Jump:** The CPU instruction pointer (`RIP`) is moved to the start of the `Main()` function in the generated memory block.
4. **Terminate:** Upon completion of `Main()`, the process exits with the returned integer code.

### 7.2 Usage Examples

#### Case A: Running the Application

The standard way to launch the system defined in *Appendix A*.

```bash
$ vanarize main.vana

[Vanarize JIT] Compiling... (4ms)
[Vanarize JIT] Execution started.
Vanarize System Initializing...
Attempt 1...
Uploading data for: Therm-X100
Upload Successful.
Operation time: 4200ns

```

#### Case B: Debugging Generated Assembly

For development and verification purposes, the compiler accepts flags to dump the generated machine code. This confirms the "Direct-to-Metal" architecture.

```bash
$ vanarize --dump-asm main.vana

[JIT DUMP] Function: CalculateVector
0x7F...00:  55          push   rbp
0x7F...01:  48 89 E5    mov    rbp, rsp
0x7F...04:  F2 0F 59 C1 mulsd  xmm0, xmm1  ; Native multiplication
0x7F...08:  5D          pop    rbp
0x7F...09:  C3          ret
...
[Vanarize JIT] Execution started.

```

### 7.3 Error Reporting

In the event of a syntax error or runtime exception (e.g., Type Mismatch during compilation), the CLI must output the exact file, line number, and a visual pointer to the offending token.

```bash
$ vanarize server.vana

Error in 'server.vana' at line 42:
42 |    number port = "8080"
                      ^
Type Mismatch: Cannot assign type 'text' to variable of type 'number'.

```

---

**End of Specification.**
*Approved for Implementation.*