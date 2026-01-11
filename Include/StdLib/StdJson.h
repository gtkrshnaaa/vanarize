#ifndef VANARIZE_STDLIB_STDJSON_H
#define VANARIZE_STDLIB_STDJSON_H

#include "Core/VanarizeValue.h"

/**
 * StdJson - Zero-copy JSON parser
 * MASTERPLAN Section 5.4: FSA-based JSON parsing
 */

// Parse JSON string into Value (struct-like object)
Value StdJson_Parse(Value jsonString);

// Convert Value (struct) to JSON string
Value StdJson_Stringify(Value obj);

// Get field value from parsed JSON object
Value StdJson_GetValue(Value obj, Value key);

#endif // VANARIZE_STDLIB_STDJSON_H
