#include <stdio.h>
#include "Compiler/Parser.h"
#include "Jit/CodeGen.h"

int main() {
    printf("Testing Functions...\n");
    
    // Test: Define 'add' function and call it.
    // function add(a, b) { return a + b; }
    // var result = add(10, 20);
    // print(result);
    
    const char* src = 
        "function add(a, b) { return a + b; }"
        "var result = add(10, 20);"
        "print(result);";
        
    printf("Source: %s\n", src);
    
    Parser_Init(src);
    AstNode* root = Parser_ParseExpression(); // Block
    JitFunction func = Jit_Compile(root);
    func();
    
    printf("Done. Expected: 30\n");
    return 0;
}
