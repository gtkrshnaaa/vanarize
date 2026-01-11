#include "Core/VanarizeObject.h"
#include "Core/Memory.h"
#include "Core/GarbageCollector.h"
#include <stdlib.h>
#include "Core/GarbageCollector.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // for malloc/realloc/free

ObjString* AsString(Value value) {
    if (!IsString(value)) return NULL;
    return (ObjString*)ValueToObj(value);
}

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)MemAlloc(size);
    object->type = type;
    object->isMarked = false;
    GC_RegisterObject(object);
    return object;
}

ObjString* NewString(const char* chars, int length) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    // Copy chars
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->chars[length] = '\0';
    return string;
}

// Array Implementation
ObjArray* Runtime_NewArray(int capacity) {
    printf("[Runtime] NewArray Checkpoint 1\n");
    ObjArray* array = (ObjArray*)GC_Allocate(sizeof(ObjArray));
    printf("[Runtime] NewArray Checkpoint 2\n");
    array->obj.type = OBJ_ARRAY;
    array->count = 0;
    array->capacity = capacity;
    array->elements = malloc(sizeof(Value) * capacity);
    printf("[Runtime] NewArray Allocated %p. Elements %p\n", array, array->elements);
    return array;
}

void Runtime_ArrayPush(ObjArray* arr, Value val) {
    if (!arr) { printf("FATAL: Push to NULL Array\n"); exit(1); }
    printf("[Runtime] Push to %p Val %lx\n", arr, val);
    if (arr->count >= arr->capacity) {
        arr->capacity *= 2;
        arr->elements = realloc(arr->elements, sizeof(Value) * arr->capacity);
    }
    arr->elements[arr->count++] = val;
}

Value Runtime_ArrayGet(ObjArray* arr, int index) {
    if (index < 0 || index >= arr->count) {
        fprintf(stderr, "Array Index Out of Bounds: %d (Size: %d)\n", index, arr->count);
        exit(1);
    }
    return arr->elements[index];
}

void Runtime_ArraySet(ObjArray* arr, int index, Value val) {
    if (index < 0 || index >= arr->count) {
        fprintf(stderr, "Array Index Out of Bounds: %d (Size: %d)\n", index, arr->count);
        exit(1);
    }
    arr->elements[index] = val;
}

int Runtime_ArrayLength(ObjArray* arr) {
    if (!arr) { printf("FATAL: Length of NULL\n"); exit(1); }
    printf("[Runtime] Length of %p is %d\n", arr, arr->count);
    return arr->count;
}

Value Runtime_ArrayPop(ObjArray* arr) {
    if (arr->count == 0) return VAL_NULL;
    return arr->elements[--arr->count];
}
