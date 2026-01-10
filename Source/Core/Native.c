#include "Core/Native.h"
#include "Core/VanarizeObject.h"
#include <stdio.h>

void Native_Print(Value val) {
    if (IsNumber(val)) {
        double num = ValueToNumber(val);
        // Print as integer if it's a whole number, otherwise as float
        if (num == (long)num) {
            printf("%ld\n", (long)num);
        } else {
            printf("%g\n", num);
        }
    } else if (IsString(val)) {
        printf("%s\n", AsCString(val));
    } else if (IsBool(val)) {
        printf("%s\n", ValueToBool(val) ? "true" : "false");
    } else if (IsNil(val)) {
        printf("nil\n");
    } else {
        printf("Unknown Value: %lx\n", val);
    }
}
