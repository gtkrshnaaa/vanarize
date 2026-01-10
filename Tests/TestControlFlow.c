#include <stdio.h>
#include "Compiler/Parser.h"
#include "Jit/CodeGen.h"

int main() {
    printf("Testing Control Flow...\n");
    
    // Test 1: If/Else
    // Note: We don't have comparison operators yet (> or ==) in CodeGen for binary ops.
    // So we use 'true'/'false' directly.
    printf("--- Test 1: If (true) ---\n");
    const char* src1 = "if (true) { print(1); } else { print(2); }";
    Parser_Init(src1);
    JitFunction func1 = Jit_Compile(Parser_ParseExpression());
    func1();
    
    // Test 2: If (false)
    printf("--- Test 2: If (false) ---\n");
    const char* src2 = "if (false) { print(1); } else { print(2); }";
    Parser_Init(src2);
    JitFunction func2 = Jit_Compile(Parser_ParseExpression());
    func2();
    
    // Test 3: While
    // "var i = 1; while (i) { print(i); i = false; }"
    printf("--- Test 3: While ---\n");
    // Note: 'i' is truthy (1). In loop, i becomes false.
    // Expected output: "Loop" then exit.
    const char* src3 = "var k = true; while (k) { print(\"Loop\"); k = false; }";
    printf("Source 3: %s\n", src3);
    
    // We need to re-init parser cleanly or just parse new string.
    // Parser_Init resets state.
    Parser_Init(src3);
    AstNode* root3 = Parser_ParseExpression(); // Block
    JitFunction func3 = Jit_Compile(root3);
    func3();
    
    printf("Done.\n");
    return 0;
}
