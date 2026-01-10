#ifndef VANARIZE_STDLIB_STDMATH_H
#define VANARIZE_STDLIB_STDMATH_H

#include "Core/VanarizeValue.h"

Value StdMath_Sin(Value arg);
Value StdMath_Cos(Value arg);
Value StdMath_Tan(Value arg);
Value StdMath_Sqrt(Value arg);
Value StdMath_Pow(Value base, Value exp);
Value StdMath_Abs(Value arg);
Value StdMath_Floor(Value arg);
Value StdMath_Ceil(Value arg);

#endif // VANARIZE_STDLIB_STDMATH_H
