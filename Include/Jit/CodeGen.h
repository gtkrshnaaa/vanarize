#ifndef VANARIZE_JIT_CODEGEN_H
#define VANARIZE_JIT_CODEGEN_H

#include "Compiler/Ast.h"
#include <stdint.h>

typedef uint64_t (*JitFunction)(void);

// Compiles an AST into executable memory and returns a function pointer to it.
// Note: This allocates new executable memory every time.
JitFunction Jit_Compile(AstNode* node);

#endif // VANARIZE_JIT_CODEGEN_H
