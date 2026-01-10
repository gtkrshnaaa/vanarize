#include <stdio.h>
#include <assert.h>
#include "Compiler/Parser.h"
#include "Jit/CodeGen.h"

int main() {
    printf("Testing Compiler Integration (Source -> AST -> JIT -> x64)...\n");
    
    const char* source = "10 + 20 + 30";
    printf("Source: %s\n", source);
    
    // 1. Parse
    Parser_Init(source);
    AstNode* root = Parser_ParseExpression();
    assert(root != NULL);
    printf("Parsed AST.\n");
    
    // 2. Compile
    JitFunction func = Jit_Compile(root);
    assert(func != NULL);
    printf("Compiled to JIT code at %p.\n", func);
    
    // 3. Run
    uint64_t result = func();
    printf("Result execution: %lu\n", result);
    
    assert(result == 60);
    
    printf("Integration OK.\n");
    return 0;
}
