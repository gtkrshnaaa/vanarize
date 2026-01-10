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
            if (decl->initializer) {
                emitNode(as, decl->initializer, ctx);
            } else {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
            }
            Asm_Push(as, RAX);
            ctx->stackSize += 8;
            Local* local = &ctx->locals[ctx->localCount++];
            local->name = decl->name;
            local->offset = ctx->stackSize;
            break;
        }

        case NODE_ASSIGNMENT_EXPR: {
            AssignmentExpr* assign = (AssignmentExpr*)node;
            // 1. Compile Value -> RAX
            emitNode(as, assign->value, ctx);
            
            // 2. Resolve Variable
            int offset = resolveLocal(ctx, &assign->name);
            if (offset == 0) {
                 fprintf(stderr, "JIT Error: Undefined variable '%.*s' in assignment\n", assign->name.length, assign->name.start);
                 exit(1);
            }
            
            // 3. Store RAX -> [RBP - offset]
            Asm_Mov_Mem_Reg(as, RBP, -offset, RAX);
            // Result of assignment is the value (RAX preserved)
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

        case NODE_IF_STMT: {
            IfStmt* stmt = (IfStmt*)node;
            // 1. Compile Condition
            emitNode(as, stmt->condition, ctx);
            
            // 2. Check if false (Simplified: Assume boolean values are 2 for FALSE and 3 for TRUE.
            // Actually, we can just compare with VAL_FALSE (2).
            // But if it's nil, that's also falsey?
            // For now: Compare RAX with VAL_FALSE. If equal, Jump Else.
            // NOTE: VAL_FALSE is a 64-bit value with NaNs.
            // Asm_Cmp_Reg_Imm only supports 32-bit immediate. 
            // We need 64-bit comparison? Or just check low bits?
            // Val: False=2 (Actually ...7FFC...02).
            // Let's implement full 64-bit compare logic or just Asm_Mov_Imm64(RCX, VAL_FALSE); Cmp RAX, RCX?
            // We don't have Cmp_Reg_Reg yet? I forgot to declare it in Replace 181?
            // Checking AssemblerX64.h... I commented it out/didn't implement Cmp_Reg_Reg properly?
            // I only added Cmp_Reg_Imm.
            // Can we compare against 0 (nil)? Or raw true/false.
            // Hack: Compare against VAL_FALSE using a temp register.
            // Since we know VAL_FALSE is constant.
            // BUT Asm_Cmp_Reg_Imm is limited to 32-bit.
            // Let's add Asm_Cmp_Reg_Reg locally if needed, or use a scratch register.
            // Actually, testing against 0 (boolean false) if we use raw integers would be easier.
            // But we are sticking to Vanarize values.
            // Let's assume for this specific test, condition is `true` or `false`.
            // Let's verify Asm_Cmp_Reg_Imm implementation... it emits 48 81 /7 id.
            // If we use 0 and 1 for booleans in our test, simpler.
            // But let's try to be correct.
            // Let's use `CMP RAX, Imm32` if value fits in 32 bits?
            // QNAN markers don't fit.
            // OK, let's load VAL_FALSE into RCX and CMP RAX, RCX.
            // We need `CMP r64, r64`.
            // Opcode is `48 39 /r` (CMP r/m64, r64).
            // Let's emit raw bytes for now since I forgot to expose it cleanly.
            // 48 39 /r => ModRM.
            
            // Actually, we can use `Asm_Cmp_Reg_Imm` if we assume 0 = false/null and 1 = true
            // But let's support generic values.
            // Load VAL_FALSE to RCX.
            Asm_Mov_Imm64(as, RCX, VAL_FALSE);
            
            // CMP RAX, RCX
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8); // Mode=11, Reg=RCX(1), RM=RAX(0) -> 11 001 000 = C8.
            
            // JE elseBranch (Jump if Equal to False)
            // Emit JE with 0 offset
            size_t elseJumpPatch = as->offset + 2; // Offset of the displacement bytes (after 0F 84)
            Asm_Je(as, 0); 
            
            // Compile Then
            emitNode(as, stmt->thenBranch, ctx);
            
            // JMP end
            size_t endJumpPatch = as->offset + 1; // Offset of displacement (after E9)
            Asm_Jmp(as, 0);
            
            // Patch JE to here (start of Else)
            size_t elseStart = as->offset;
            int32_t jumpDist = (int32_t)(elseStart - (elseJumpPatch + 4));
            Asm_Patch32(as, elseJumpPatch, jumpDist);
            
            // Compile Else
            if (stmt->elseBranch) {
                emitNode(as, stmt->elseBranch, ctx);
            }
            
            // Patch JMP to here (End)
            size_t endStart = as->offset;
            int32_t endDist = (int32_t)(endStart - (endJumpPatch + 4));
            Asm_Patch32(as, endJumpPatch, endDist);
            
            break;
        }

        case NODE_WHILE_STMT: {
            WhileStmt* stmt = (WhileStmt*)node;
            size_t loopStart = as->offset;
            
            // 1. Compile Condition
            emitNode(as, stmt->condition, ctx);
            
            // 2. CMP RAX, VAL_FALSE
            Asm_Mov_Imm64(as, RCX, VAL_FALSE);
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8);
            
            // 3. JE end
            size_t loopEndPatch = as->offset + 2;
            Asm_Je(as, 0);
            
            // 4. Body
            emitNode(as, stmt->body, ctx);
            
            // 5. JMP start (Backwards)
            size_t jmpBackStart = as->offset;
            // Dist = Target - (Source + 5)
            // Target = loopStart. Source = jmpBackStart.
            // Dist = loopStart - (jmpBackStart + 5).
            int32_t backDist = (int32_t)(loopStart - (jmpBackStart + 5));
            Asm_Jmp(as, backDist);
            
            // 6. Patch JE to end
            size_t endOffset = as->offset;
            int32_t exitDist = (int32_t)(endOffset - (loopEndPatch + 4));
            Asm_Patch32(as, loopEndPatch, exitDist);
            
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
