#include <stdio.h>
#include "Compiler/Parser.h"
#include "Jit/CodeGen.h"

int main() {
    printf("Testing Hello World (print(\"Hello\"))...\n");
    
    // Test 1: Compile simple expression 10+20 just to be sure we didn't break it
    // But our CodeGen produces 60? No, that was Parser/JIT combo.
    // Let's stick to the new feature.
    
    const char* source = "print(\"Hello Vanarize\")";
    printf("Source: %s\n", source);
    
    Parser_Init(source);
    AstNode* root = Parser_ParseExpression();
    
    if (root->type != NODE_CALL_EXPR) {
        printf("Error: Root is not CallExpr\n");
        return 1;
    }
    
    // Compile
    JitFunction func = Jit_Compile(root);
    
    // Run
    printf("Executing...\n");
    func();
    
    // Test 2: print(true)
    const char* src2 = "print(true)";
    printf("Source 2: %s\n", src2);
    Parser_Init(src2);
    AstNode* root2 = Parser_ParseExpression();
    JitFunction func2 = Jit_Compile(root2);
    func2();
    
    printf("Done.\n");
    return 0;
}
