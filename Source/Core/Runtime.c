#include "Core/Runtime.h"
#include "Core/VanarizeObject.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward decls from other files if needed
Value NumberToValue(double num);
double ValueToNumber(Value v);
ObjString* NewString(const char* chars, int length);

Value Runtime_Add(Value a, Value b) {
    if (IsNumber(a) && IsNumber(b)) {
        return NumberToValue(ValueToNumber(a) + ValueToNumber(b));
    }
    
    if (IsString(a) && IsString(b)) {
        ObjString* s1 = AsString(a);
        ObjString* s2 = AsString(b);
        
        int len = s1->length + s2->length;
        char* chars = malloc(len + 1);
        memcpy(chars, s1->chars, s1->length);
        memcpy(chars + s1->length, s2->chars, s2->length);
        chars[len] = '\0';
        
        ObjString* res = NewString(chars, len);
        free(chars); 
        return ObjToValue((Obj*)res);
    }
    
    // String + Number
    if (IsString(a) && IsNumber(b)) {
        ObjString* s1 = AsString(a);
        double num = ValueToNumber(b);
        char buffer[64];
        int numLen = snprintf(buffer, 64, "%.14g", num); // Use %.14g to match print
        
        int len = s1->length + numLen;
        char* chars = malloc(len + 1);
        memcpy(chars, s1->chars, s1->length);
        memcpy(chars + s1->length, buffer, numLen);
        chars[len] = '\0';
        
        ObjString* res = NewString(chars, len);
        free(chars);
        return ObjToValue((Obj*)res);
    }

    // Number + String
    if (IsNumber(a) && IsString(b)) {
        double num = ValueToNumber(a);
        ObjString* s2 = AsString(b);
        char buffer[64];
        int numLen = snprintf(buffer, 64, "%.14g", num);
        
        int len = numLen + s2->length;
        char* chars = malloc(len + 1);
        memcpy(chars, buffer, numLen);
        memcpy(chars + numLen, s2->chars, s2->length);
        chars[len] = '\0';
        
        ObjString* res = NewString(chars, len);
        free(chars);
        return ObjToValue((Obj*)res);
    }
}

Value Runtime_Equal(Value a, Value b) {
    if (a == b) return VAL_TRUE;
    // Check deep equality for strings?
    // NaN boxing makes identical strings (pointers) equal.
    // But interned strings? If strings are interned, pointer equality is enough.
    // If not interned, need strcmp.
    // For now: value equality.
    return VAL_FALSE;
}
