#include "Jit/CodeGen.h"
#include "Compiler/Parser.h"
#include "Core/Native.h"
#include <stdio.h>

int main() {
    printf("Testing Structs...\n");
    const char* src = 
        "struct Point {\n"
        "    number x\n"
        "    number y\n"
        "}\n"
        "Point p = { x: 10, y: 20 };\n"
        "print(p.x + p.y);\n";
        
    Parser_Init(src);
    AstNode* root = Parser_ParseExpression();
    JitFunction func = Jit_Compile(root);
    func();
    
    printf("Done. Expected: 30\n");
    return 0;
}
