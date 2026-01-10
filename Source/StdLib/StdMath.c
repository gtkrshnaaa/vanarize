#include "StdLib/StdMath.h"
#include <math.h>

Value StdMath_Sin(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(sin(ValueToNumber(arg)));
}

Value StdMath_Cos(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(cos(ValueToNumber(arg)));
}

Value StdMath_Tan(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(tan(ValueToNumber(arg)));
}

Value StdMath_Sqrt(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(sqrt(ValueToNumber(arg)));
}

Value StdMath_Pow(Value base, Value exp) {
    double b = IsNumber(base) ? ValueToNumber(base) : 0.0;
    double e = IsNumber(exp) ? ValueToNumber(exp) : 0.0;
    return NumberToValue(pow(b, e));
}

Value StdMath_Abs(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(fabs(ValueToNumber(arg)));
}

Value StdMath_Floor(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(floor(ValueToNumber(arg)));
}

Value StdMath_Ceil(Value arg) {
    if (!IsNumber(arg)) return NumberToValue(0.0);
    return NumberToValue(ceil(ValueToNumber(arg)));
}
