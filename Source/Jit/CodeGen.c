#include "Jit/CodeGen.h"
#include "Jit/AssemblerX64.h"
#include "Jit/ExecutableMemory.h"
#include "Core/Native.h"
#include "Core/VanarizeObject.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_JIT_SIZE 4096

// Simple Symbol Table for Locals
typedef struct {
    Token name;
    int offset; // RBP offset (negative)
} Local;

typedef struct {
    Local locals[64];
    int localCount;
    int stackSize;
} CompilerContext;

static int resolveLocal(CompilerContext* ctx, Token* name) {
    for (int i = 0; i < ctx->localCount; i++) {
        Token* localName = &ctx->locals[i].name;
        if (localName->length == name->length && 
            memcmp(localName->start, name->start, name->length) == 0) {
            return ctx->locals[i].offset;
        }
    }
    return 0; // Not found
}

static void emitNode(Assembler* as, AstNode* node, CompilerContext* ctx) {
    switch (node->type) {
        case NODE_BLOCK: {
            BlockStmt* block = (BlockStmt*)node;
            for (int i = 0; i < block->count; i++) {
                emitNode(as, block->statements[i], ctx);
            }
            break;
        }
        
        case NODE_VAR_DECL: {
            VarDecl* decl = (VarDecl*)node;
            // 1. Compile Initializer -> RAX
            if (decl->initializer) {
                emitNode(as, decl->initializer, ctx);
            } else {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
            }
            
            // 2. Push to stack (Alloc local)
            // Ideally we pre-calc stack size. For JIT simplicity, we can just push.
            // But strict offsets are better.
            Asm_Push(as, RAX); // RSP decrements by 8
            
            // 3. Register local
            ctx->stackSize += 8;
            Local* local = &ctx->locals[ctx->localCount++];
            local->name = decl->name;
            local->offset = ctx->stackSize; // Offset from RBP?
            // RBP points to old RBP.
            // Stack: [Old RBP] [Ret Addr] ...
            // Wait, Standard Prologue: PUSH RBP; MOV RBP, RSP.
            // Locals are at RBP - 8, RBP - 16 etc.
            // If we use PUSH to store locals, they appear at RBP - 8 * N.
            // Yes.
            break;
        }

        case NODE_LITERAL_EXPR: {
            LiteralExpr* lit = (LiteralExpr*)node;
            if (lit->token.type == TOKEN_NUMBER) {
                // ... (Number parsing same as before)
                char buffer[64];
                int len = lit->token.length < 63 ? lit->token.length : 63;
                for(int i=0; i<len; i++) buffer[i] = lit->token.start[i];
                buffer[len] = '\0';
                uint64_t val = strtoull(buffer, NULL, 10);
                Asm_Mov_Imm64(as, RAX, val);
            } else if (lit->token.type == TOKEN_IDENTIFIER) {
                // Resolve Variable
                int offset = resolveLocal(ctx, &lit->token);
                if (offset == 0) {
                     fprintf(stderr, "JIT Error: Undefined variable '%.*s'\n", lit->token.length, lit->token.start);
                     exit(1);
                }
                // Load from [RBP - offset] -> RAX
                Asm_Mov_Reg_Mem(as, RAX, RBP, -offset);
            } 
            // ... (True/False/Nil/String handling same as before)
            // Copy paste for brevity or keep structure
             else if (lit->token.type == TOKEN_TRUE) {
                Asm_Mov_Imm64(as, RAX, VAL_TRUE);
            } else if (lit->token.type == TOKEN_FALSE) {
                Asm_Mov_Imm64(as, RAX, VAL_FALSE);
            } else if (lit->token.type == TOKEN_NIL) {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
            }
            break;
        }
        
        // ... (Calls, Strings, BinaryExpr same, pass ctx)
        case NODE_STRING_LITERAL: {
             // ... same
            StringExpr* strExpr = (StringExpr*)node;
            int len = strExpr->token.length - 2;
            ObjString* objStr = malloc(sizeof(ObjString) + len + 1);
            objStr->obj.type = OBJ_STRING;
            objStr->length = len;
            memcpy(objStr->chars, strExpr->token.start + 1, len);
            objStr->chars[len] = '\0';
            Value v = ObjToValue(objStr);
            Asm_Mov_Imm64(as, RAX, v);
            break;
        }
        
        case NODE_CALL_EXPR: {
             // ... same
            CallExpr* call = (CallExpr*)node;
            if (call->argCount > 0) {
                emitNode(as, call->args[0], ctx);
                Asm_Push(as, RAX);
                Asm_Pop(as, RDI);
            }
            void* funcPtr = (void*)Native_Print;
            Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
            Asm_Call_Reg(as, RAX);
            break;
        }

        case NODE_BINARY_EXPR: {
            BinaryExpr* bin = (BinaryExpr*)node;
            emitNode(as, bin->left, ctx);
            Asm_Push(as, RAX);
            emitNode(as, bin->right, ctx);
            Asm_Pop(as, RCX); 
            // Note: Pops into RCX, but Left was pushed.
            // If Stack increased due to locals, we must be careful.
            // Expression evaluation uses temp stack which is effectively "above" locals.
            // Since we push/pop relative to RSP, locals remain "below".
            // RBP is fixed anchor. Locals at [RBP-8].
            // RSP moves.
            // So Push/Pop for expression evaluation works fine ON TOP of locals.
            
            if (bin->op.type == TOKEN_PLUS) {
                Asm_Add_Reg_Reg(as, RAX, RCX);
            } 
            break;
        }

        default: break;
    }
}

JitFunction Jit_Compile(AstNode* node) {
    void* mem = Jit_AllocExec(MAX_JIT_SIZE);
    
    Assembler as;
    Asm_Init(&as, (uint8_t*)mem, MAX_JIT_SIZE);
    
    // Prologue: PUSH RBP; MOV RBP, RSP
    Asm_Push(&as, RBP);
    Asm_Mov_Reg_Reg(&as, RBP, RSP);
    // Note: Asm_Mov_Reg_Reg is implemented properly?
    // Wait, check implementation.
    // Yes: 48 89 /r -> ModRM(Direct).
    
    CompilerContext ctx;
    ctx.localCount = 0;
    ctx.stackSize = 0;

    emitNode(&as, node, &ctx);
    
    // Epilogue: MOV RSP, RBP; POP RBP; RET
    Asm_Mov_Reg_Reg(&as, RSP, RBP);
    Asm_Pop(&as, RBP);
    Asm_Ret(&as);
    
    return (JitFunction)mem;
}
