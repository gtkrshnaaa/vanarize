#ifndef VANARIZE_CORE_RUNTIME_H
#define VANARIZE_CORE_RUNTIME_H

#include "Core/VanarizeValue.h"

// Runtime helpers for JIT
Value Runtime_Add(Value a, Value b);
Value Runtime_Equal(Value a, Value b);

#endif
