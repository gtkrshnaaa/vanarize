#include "Core/Native.h"
#include "Core/VanarizeObject.h"
#include <stdio.h>

void Native_Print(Value val) {
    if (IsNumber(val)) {
        // Warning: using raw int for tests, but normally double
        printf("%ld\n", (long)val);
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
