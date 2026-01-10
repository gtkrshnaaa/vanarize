#include <stdio.h>
#include "Compiler/Parser.h"
#include "Jit/CodeGen.h"

int main() {
    printf("Testing Variables...\n");
    
    // Note: Our Parser now expects statements ending in semicolon
    const char* source = "var x = 10; var y = 20; print(x + y);";
    printf("Source: %s\n", source);
    
    Parser_Init(source);
    AstNode* root = Parser_ParseExpression(); // Parses block of decls
    
    JitFunction func = Jit_Compile(root);
    func();
    
    printf("Done. Expected: 30\n");
    return 0;
}
