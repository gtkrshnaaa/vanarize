#ifndef VANARIZE_CORE_OBJECT_H
#define VANARIZE_CORE_OBJECT_H

#include "Core/VanarizeValue.h"
#include <string.h>

typedef enum {
    OBJ_STRING,
    OBJ_STRUCT,
    OBJ_FUNCTION
} ObjType;

typedef struct Obj Obj;

struct Obj {
    ObjType type;
    Obj* next; // GC chain
};

typedef struct {
    Obj obj;
    int length;
    char chars[]; // Flexible array member
} ObjString;

static inline ObjType GetObjType(Value v) {
    Obj* obj = (Obj*)ValueToObj(v);
    return obj->type;
}

static inline bool IsString(Value v) {
    return IsObj(v) && GetObjType(v) == OBJ_STRING;
}

static inline char* AsCString(Value v) {
    ObjString* str = (ObjString*)ValueToObj(v);
    return str->chars;
}

#endif // VANARIZE_CORE_OBJECT_H
