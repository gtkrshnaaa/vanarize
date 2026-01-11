#ifndef VANARIZE_CORE_OBJECT_H
#define VANARIZE_CORE_OBJECT_H

#include "Core/VanarizeValue.h"
#include <stdbool.h>
#include <string.h>

typedef enum {
    OBJ_STRING,
    OBJ_STRUCT,
    OBJ_FUNCTION,
    OBJ_ARRAY
} ObjType;

typedef struct Obj Obj;

struct Obj {
    ObjType type;
    bool isMarked;       // GC marking flag
    struct Obj* next;    // Intrusive linked list of all objects
};

typedef struct {
    Obj obj;
    int length;
    char chars[]; // Flexible array member
} ObjString;

typedef struct {
    Obj obj;
    void* entrypoint; // JIT Code Pointer
    int arity;

    ObjString* name;
} ObjFunction;

typedef struct {
    Obj obj;
    uint32_t size;         // Total size in bytes of the data blob
    uint64_t pointerBitmap; // Bit N is 1 if byte-offset N is start of a Pointer
    uint8_t data[];        // Packed data (flexible array)
} ObjStruct;

typedef struct {
    Obj obj;
    int count;
    int capacity;
    Value* elements; // Pointer to heap array of Values
} ObjArray;

// Helper to check object type
ObjString* AsString(Value value);

// Helper to check object type
static inline ObjType GetObjType(Value value) {
    if (!IsObj(value)) return (ObjType)-1;
    Obj* obj = (Obj*)ValueToObj(value);
    return obj->type;
}

static inline bool IsString(Value value) {
    return GetObjType(value) == OBJ_STRING;
}

static inline char* AsCString(Value v) {
    ObjString* str = (ObjString*)ValueToObj(v);
    return str->chars;
}

// Array Implementation
ObjArray* Runtime_NewArray(int capacity);
void Runtime_ArrayPush(ObjArray* arr, Value val);
Value Runtime_ArrayGet(ObjArray* arr, int index);
void Runtime_ArraySet(ObjArray* arr, int index, Value val);
int Runtime_ArrayLength(ObjArray* arr);
Value Runtime_ArrayPop(ObjArray* arr);

#endif // VANARIZE_CORE_OBJECT_H
