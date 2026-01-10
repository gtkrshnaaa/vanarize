#include <stdio.h>
#include <stdlib.h>
#include "Compiler/Parser.h"
#include "Jit/CodeGen.h"
#include "Core/Memory.h"
#include "Core/GarbageCollector.h"

char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    
    fclose(file);
    return buffer;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: vanarize [path]\n");
        exit(64);
    }
    
    char* source = readFile(argv[1]);
    
    Parser_Init(source);
    AstNode* root = Parser_ParseProgram();  // Parse entire program
    if (!root) {
        fprintf(stderr, "Parse error.\n");
        exit(65);
    }
    
    // Initialize GC and Memory
    // Initialize Core
    int stackDummy;
    VM_InitMemory();
    GC_Init(&stackDummy);
    
    JitFunction func = Jit_Compile(root);
    func();
    
    free(source);
    return 0;
}
