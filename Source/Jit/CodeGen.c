#include "Jit/CodeGen.h"
#include "Jit/AssemblerX64.h"
#include "Jit/ExecutableMemory.h"
#include "Core/Native.h"
#include "Core/VanarizeObject.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_JIT_SIZE 4096

static void emitNode(Assembler* as, AstNode* node) {
    switch (node->type) {
        case NODE_LITERAL_EXPR: {
            LiteralExpr* lit = (LiteralExpr*)node;
            if (lit->token.type == TOKEN_NUMBER) {
                // ... same number parsing
                char buffer[64];
                int len = lit->token.length < 63 ? lit->token.length : 63;
                for(int i=0; i<len; i++) buffer[i] = lit->token.start[i];
                buffer[len] = '\0';
                uint64_t val = strtoull(buffer, NULL, 10);
                Asm_Mov_Imm64(as, RAX, val);
            } else if (lit->token.type == TOKEN_TRUE) {
                Asm_Mov_Imm64(as, RAX, VAL_TRUE);
            } else if (lit->token.type == TOKEN_FALSE) {
                Asm_Mov_Imm64(as, RAX, VAL_FALSE);
            } else if (lit->token.type == TOKEN_NIL) {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
            }
            break;
        }

        case NODE_STRING_LITERAL: {
            StringExpr* strExpr = (StringExpr*)node;
            // 1. Create native ObjString (Runtime)
            // Need to extract string from token (remove quotes)
            int len = strExpr->token.length - 2;
            ObjString* objStr = malloc(sizeof(ObjString) + len + 1);
            objStr->obj.type = OBJ_STRING;
            objStr->length = len;
            memcpy(objStr->chars, strExpr->token.start + 1, len);
            objStr->chars[len] = '\0';
            
            // 2. Wrap in Value (NaN Boxed)
            Value v = ObjToValue(objStr);
            
            // 3. Emit MOV RAX, POINTER
            // We mov the Value (which is just a flagged pointer)
            Asm_Mov_Imm64(as, RAX, v);
            break;
        }

        case NODE_CALL_EXPR: {
            CallExpr* call = (CallExpr*)node;
            // 1. Compile Arguments
            // System V ABI: First arg in RDI.
            // Start with single arg for PRINT
            if (call->argCount > 0) {
                emitNode(as, call->args[0]);
                // Result in RAX. Move to RDI.
                // We don't have MOV RDI, RAX yet in Assembler?
                // We have Asm_Mov_Reg_Reg? No, we didn't implement it fully.
                // Quick hack: Push RAX, Pop RDI.
                Asm_Push(as, RAX);
                Asm_Pop(as, RDI);
                
                // TODO: Handle more args (RSI, RDX...)
            }
            
            // 2. Identify Callee
            // Assume it is "print"
            // Get pointer to Native_Print
            void* funcPtr = (void*)Native_Print;
            
            // 3. MOV RAX, funcPtr
            Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
            
            // 4. CALL RAX
            Asm_Call_Reg(as, RAX);
            break;
        }
        
        case NODE_BINARY_EXPR: {
            BinaryExpr* bin = (BinaryExpr*)node;
            
            // 1. Compile Left -> RAX
            emitNode(as, bin->left);
            
            // 2. Push RAX
            Asm_Push(as, RAX);
            
            // 3. Compile Right -> RAX
            emitNode(as, bin->right);
            
            // 4. Pop Left -> RCX
            Asm_Pop(as, RCX);
            
            // 5. Operation
            if (bin->op.type == TOKEN_PLUS) {
                // RCX = Left, RAX = Right
                // We want Result in RAX.
                // ADD Left, Right -> Left += Right.
                // ADD RCX, RAX -> RCX has result.
                // MOV RAX, RCX
                Asm_Add_Reg_Reg(as, RAX, RCX);
            } else if (bin->op.type == TOKEN_STAR) {
                // Not implemented in Assembler yet
                fprintf(stderr, "JIT Error: Multiply not implemented yet.\n");
                exit(1);
            }
            break;
        }
        
        default:
            fprintf(stderr, "JIT Error: Unsupported node type %d\n", node->type);
            exit(1);
    }
}

JitFunction Jit_Compile(AstNode* node) {
    void* mem = Jit_AllocExec(MAX_JIT_SIZE);
    
    Assembler as;
    Asm_Init(&as, (uint8_t*)mem, MAX_JIT_SIZE);
    
    // Prologue (None for simple leaf functions yet)
    
    // Body
    emitNode(&as, node);
    
    // Epilogue
    Asm_Ret(&as);
    
    return (JitFunction)mem;
}
