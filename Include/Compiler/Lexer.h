#ifndef VANARIZE_COMPILER_LEXER_H
#define VANARIZE_COMPILER_LEXER_H

#include "Compiler/Token.h"

void Lexer_Init(const char* source);
Token Lexer_NextToken(void);

#endif // VANARIZE_COMPILER_LEXER_H
