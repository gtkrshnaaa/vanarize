#ifndef VANARIZE_COMPILER_LEXER_H
#define VANARIZE_COMPILER_LEXER_H

#include "Compiler/Token.h"

void Lexer_Init(const char* source);
Token Lexer_NextToken(void);

typedef struct {
    const char* start;
    const char* current;
    int line;
} LexerState;

LexerState Lexer_GetState(void);
void Lexer_RestoreState(LexerState state);

#endif // VANARIZE_COMPILER_LEXER_H
