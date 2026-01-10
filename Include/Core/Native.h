#ifndef VANARIZE_CORE_NATIVE_H
#define VANARIZE_CORE_NATIVE_H

#include "Core/VanarizeValue.h"

// Defined in Main.c or Native.c, called by JIT
void Native_Print(Value val);

#endif // VANARIZE_CORE_NATIVE_H
