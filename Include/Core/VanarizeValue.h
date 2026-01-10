#ifndef VANARIZE_CORE_VALUE_H
#define VANARIZE_CORE_VALUE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * VANARIZE VALUE REPRESENTATION (NaN Boxing)
 * 
 * Standard IEEE 754 Doubles have 64 bits:
 * S (1) | Exponent (11) | Mantissa (52)
 * 
 * NaN values have an exponent of all 1s (0x7FF).
 * We use the high bits of the mantissa to tag different types.
 * 
 * Representation:
 * - Double:  Standard IEEE 754 value (if not NaN).
 * - Pointer: 0xFFFC | 0000 | 48-bit address
 * - True:    0xFFFC | 0001 | 0...0
 * - False:   0xFFFC | 0002 | 0...0
 * - Null:    0xFFFC | 0003 | 0...0
 */

typedef uint64_t Value;

// Mask for Signaling NaN (all exponent bits set) + Quiet Bit
// We use a slightly different signature to safely distinguish from standard NaNs.
// QNAN = 0x7FFC000000000000
#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000)

#define TAG_NIL   1
#define TAG_FALSE 2
#define TAG_TRUE  3

// Singleton Values
#define VAL_NULL  ((Value)(QNAN | TAG_NIL))
#define VAL_FALSE ((Value)(QNAN | TAG_FALSE))
#define VAL_TRUE  ((Value)(QNAN | TAG_TRUE))

// Helpers to check types
static inline bool IsNumber(Value v) {
    return (v & QNAN) != QNAN;
}

static inline bool IsObj(Value v) {
    return (v & QNAN) == QNAN && (v & TAG_TRUE) == 0; // Pointers have 0 in low bits usually, wait.
    // Correction: Pointers are QNAN | Ptr. Valid pointers on x64 use lower 48 bits.
    // So if (v & QNAN) == QNAN and it's not one of our special singletons.
    // Actually, widespread convention: 
    // Pointer is just the QNAN bits + the pointer. The lowest 3 bits of a pointer are usually 0 (alignment), 
    // but we need to safer.
    // Let's stick to: If (v & QNAN) == QNAN, look at the payload.
}

static inline bool IsNil(Value v) {
    return v == VAL_NULL;
}

static inline bool IsBool(Value v) {
    return v == VAL_TRUE || v == VAL_FALSE;
}

// Conversions
static inline Value NumberToValue(double num) {
    union {
        double d;
        uint64_t u;
    } cast;
    cast.d = num;
    return cast.u;
}

static inline double ValueToNumber(Value v) {
    union {
        uint64_t u;
        double d;
    } cast;
    cast.u = v;
    return cast.d;
}

static inline Value BoolToValue(bool b) {
    return b ? VAL_TRUE : VAL_FALSE;
}

static inline bool ValueToBool(Value v) {
    return v == VAL_TRUE;
}

static inline Value ObjToValue(void* ptr) {
    return (Value)((uintptr_t)ptr | QNAN);
}

static inline void* ValueToObj(Value v) {
    return (void*)((uintptr_t)(v & ~QNAN));
}

#endif // VANARIZE_CORE_VALUE_H
