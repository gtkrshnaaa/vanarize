#ifndef VANARIZE_COMPILER_PARSER_H
#define VANARIZE_COMPILER_PARSER_H

#include "Compiler/Ast.h"

void Parser_Init(const char* source);
AstNode* Parser_ParseExpression(void);
AstNode* Parser_ParseProgram(void);  // Parse entire program

#endif // VANARIZE_COMPILER_PARSER_H
