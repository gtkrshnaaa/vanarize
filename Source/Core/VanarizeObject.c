#include "Core/VanarizeObject.h"
#include "Core/Memory.h"
#include "Core/GarbageCollector.h"
#include <string.h>

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
    return string;
}
