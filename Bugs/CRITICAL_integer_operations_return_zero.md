# Bug Report: Integer Operations Return Zero

**Date**: 2026-01-11  
**Severity**: CRITICAL  
**Status**: INVESTIGATING

## Summary
Binary arithmetic operations (`i = i + 1`) are returning 0 instead of correct result, causing infinite loops in all iterative code.

## Reproduction

### Test Case 1: Simple Increment
```vana
// File: Examples/DebugTest.vana
function Main() {
    print("Start");
    number i = 0;
    i = i + 1;
    print(i);  // Expected: 1, Actual: 0
    print("Done");
}
```

**Result**: Prints "0" instead of "1"

### Test Case 2: Loop
```vana
// File: Examples/SmallLoop.vana
function Main() {
    print("Start loop");
    number count = 0;
    for (number i = 0; i < 10; i = i + 1) {
        count = count + 1;
    }
    print(count);  // Never reached
    print("Done");
}
```

**Result**: Infinite loop, timeout after 5 seconds

## Investigation

### Commits Tested
- ✅ **86ddb2a** (origin/main) - Cannot build (unused variables)
- ❌ **afa6a20** (HEAD) - Bug present
- ❌ **After revert of integer specialization** - Bug still present

### Key Finding
Bug exists even after reverting integer specialization changes (commits 9d3be0d and 23249a2), suggesting the issue was introduced in compilation warning fixes or earlier commits.

## Suspected Root Causes

### Theory 1: Binary Expression Emission
The integer fast-path in `NODE_BINARY_EXPR` (commit 9d3be0d) may have:
- Incorrect register handling
- Missing result propagation to RAX
- Broken assignment chain

### Theory 2: Assignment Expression
`NODE_ASSIGNMENT_EXPR` type tracking (commit 57c224c or earlier) may:
- Not properly storing results
- Overwriting RAX incorrectly
- Stack/register mismatch

### Theory 3: Type Tracking Side Effects
Addition of `lastExprType` and `lastResultReg` fields may have:
- Uninitialized values affecting logic
- Incorrect propagation through expression tree

## Stack Trace
```
Examples/DebugTest.vana:
  Main() -> number i = 0 -> i = i + 1
  
JIT Emission:
  NODE_VAR_DECL (i = 0) -> MOV RAX, 0 (NaN-boxed)
  NODE_ASSIGNMENT_EXPR (i = i + 1)
    -> NODE_BINARY_EXPR (i + 1)
      -> NODE_LITERAL_EXPR (i) -> Load from register
      -> NODE_LITERAL_EXPR (1) -> Load immediate
      -> Integer fast-path: ADD (?) 
    -> Store result to i
  NODE_CALL_EXPR (print i) -> Should print 1, prints 0
```

## Assembly Analysis Needed
- [ ] Dump generated machine code for DebugTest.vana
- [ ] Verify ADD instruction is emitted correctly
- [ ] Check RAX value before and after binary operation
- [ ] Verify assignment stores correct value

## Workaround
None available - all arithmetic operations broken.

## Next Steps
1. Add assembly dump capability to JIT
2. Trace execution with GDB
3. Bisect commits to find exact introduction point
4. Consider full revert to last known good state

## Related Files
- `Source/Jit/CodeGen.c` - Binary expression and assignment emission
- `Examples/DebugTest.vana` - Minimal reproduction case  
- `Examples/SmallLoop.vana` - Loop timeout reproduction

## Notes
This bug blocks all further testing and benchmarking. Integer specialization optimization cannot be verified until basic arithmetic operations work correctly.
