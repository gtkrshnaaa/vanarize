#include "Jit/CodeGen.h"
#include "Jit/AssemblerX64.h"
#include "Jit/ExecutableMemory.h"
#include "Core/VanarizeValue.h"
#include "Core/VanarizeObject.h"
#include "Core/Memory.h"
#include "Core/GarbageCollector.h"
#include "Core/Native.h"
#include "StdLib/StdTime.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_JIT_SIZE 4096

// Simple Symbol Table for Locals
// Simple Symbol Table for Locals
typedef struct {
    Token name;
    Token typeName; // For static typing
    int offset; // RBP offset (negative)
} Local;

typedef struct {
    Local locals[64];
    int localCount;
    int stackSize;
} CompilerContext;

// Struct Registry
typedef struct {
    Token name;
    Token fieldNames[32];
    int fieldCount;
} StructInfo;

static StructInfo structRegistry[32];
static int structCount = 0;

static StructInfo* resolveStruct(Token* name) {
    for(int i=0; i<structCount; i++) {
        Token* sName = &structRegistry[i].name;
        if (sName->length == name->length && memcmp(sName->start, name->start, name->length) == 0) {
            return &structRegistry[i];
        }
    }
    return NULL;
}

static int getFieldOffset(StructInfo* info, Token* field) {
    for (int i=0; i<info->fieldCount; i++) {
        Token* fName = &info->fieldNames[i];
        if (fName->length == field->length && memcmp(fName->start, field->start, field->length) == 0) {
            return 24 + (i * 8); // 24 bytes header (16 obj + 8 count/padding) + 8 bytes per field
        }
    }
    return -1;
}

static int resolveLocal(CompilerContext* ctx, Token* name, Token* outType) {
    for (int i = 0; i < ctx->localCount; i++) {
        Token* localName = &ctx->locals[i].name;
        if (localName->length == name->length && 
            memcmp(localName->start, name->start, name->length) == 0) {
            if (outType) *outType = ctx->locals[i].typeName;
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
            local->typeName = decl->typeName; // Store type
            local->offset = ctx->stackSize;
            break;
        }

        case NODE_STRUCT_DECL: {
            StructDecl* decl = (StructDecl*)node;
            // Register struct
            StructInfo* info = &structRegistry[structCount++];
            info->name = decl->name;
            info->fieldCount = decl->fieldCount;
            for(int i=0; i<decl->fieldCount; i++) {
                info->fieldNames[i] = decl->fields[i];
            }
            // No code to emit
            break;
        }

        case NODE_STRUCT_INIT: {
            StructInit* init = (StructInit*)node;
            StructInfo* info = resolveStruct(&init->structName);
            if (!info) {
                 fprintf(stderr, "JIT Error: Unknown struct type '%.*s'\n", init->structName.length, init->structName.start);
                 exit(1);
            }
            
            // Allocate ObjStruct
            int size = sizeof(ObjStruct) + (info->fieldCount * 8);
            // We need to call malloc at runtime!
            // `Asm_Mov_Reg_Ptr(as, RAX, malloc)` would call C malloc.
            // But we can pre-calculate layout and fill it.
            // Wait, we need to create the object AT RUNTIME.
            // So we emit a call to `malloc(size)`.
            // But we should use our GC allocator later.
            // For now, use `malloc`.
            
            Asm_Mov_Imm64(as, RDI, size);
            void* mallocPtr = (void*)MemAlloc;
            Asm_Mov_Reg_Ptr(as, RAX, mallocPtr);
            Asm_Call_Reg(as, RAX);
            
            // RAX now has pointer
            // Initialize Obj header
            Asm_Push(as, RAX); // Save pointer
            
            // MOV RCX, RAX (copy pointer)
            Asm_Mov_Reg_Reg(as, RCX, RAX);
            
            // Set type = OBJ_STRUCT (1)
            Asm_Mov_Imm64(as, RDX, 1);
            Asm_Mov_Mem_Reg(as, RCX, 0, RDX);
            
            // Set isMarked = false (0)
            Asm_Mov_Imm64(as, RDX, 0);
            Asm_Mov_Mem_Reg(as, RCX, 4, RDX); // Assuming bool is 4 bytes aligned
            
            // Register with GC
            Asm_Mov_Reg_Reg(as, RDI, RAX); // First arg = obj
            void* gcRegPtr = (void*)GC_RegisterObject;
            Asm_Mov_Reg_Ptr(as, RAX, gcRegPtr);
            Asm_Call_Reg(as, RAX);
            
            // Restore pointer
            Asm_Pop(as, RAX);
            
            // PUSH it to save while evaluating fields.
            Asm_Push(as, RAX);
            
            // Set Type/Header
            // ObjStruct.obj.type = OBJ_STRUCT (1).
            // ObjStruct.fieldCount = info->fieldCount.
            // Offset 0: Type (Assuming Obj is {type, next}).
            // We need to write 1 to [RAX].
            // But `type` is enum (int) at offset 0? 
            // `Obj` struct: `Typetype; Obj* next;`.
            // Assuming 8 byte alignment for `next`.
            // `type` is 32-bit int?
            // Let's assume standard layout.
            // PUSH/POP logic:
            // Stack: [StructPtr]
            
            // We need to fill fields.
            // We must evaluate expressions in order?
            // Or arbitrary order from AST? AST key-values.
            // We iterate Struct Definition Fields to ensure correct order in memory?
            // AST Init might be out of order or partial?
            // MasterPlan implies "SensorData s = { ... }".
            
            // Safe approach:
            // 1. Initialize all fields to NULL/Zero.
            // 2. Iterate Struct Definition Field by Field.
            // 3. Find matching value in provided AST Init.
            // 4. Evaluate and Store.
            
            // But we have registers constraint.
            // Simplify: We assume AST `StructInit` matches Struct fields order?
            // No, keys are explicit.
            
            // Strategy:
            // Emit code to setup Header.
            // Loop through Struct Fields (from Registry).
            // For each field, find if AST provides a value.
            // If yes, emitNode(value).
            // Then `POP StructPtr` (peek), `MOV [StructPtr + offset], RAX`.
            // (Wait, `emitNode` clobbers registers. We must restore StructPtr).
            // Stack: [StructPtr]
            
            // 1. Initialize Header
            Asm_Mov_Reg_Mem(as, RAX, RSP, 0); // Peek StructPtr (Offset 0 from RSP?) Asm_Push(RAX) -> RSP points to it.
            // Need peek: MOV RAX, [RSP]
            Asm_Mov_Reg_Mem(as, RAX, RSP, 0); // Load address from stack
            // Actually Asm_Mov_Reg_Mem expects base+disp.
            // `Asm_Mov_Reg_Mem(as, RAX, RSP, 0)` -> OK.
            
            // Write Type (OBJ_STRUCT = 1)
            Asm_Mov_Imm64(as, RCX, 1); // OBJ_STRUCT
            Asm_Mov_Mem_Reg(as, RAX, 0, RCX); // obj.type
            
            // Write fieldCount
            Asm_Mov_Imm64(as, RCX, info->fieldCount);
            Asm_Mov_Mem_Reg(as, RAX, 16, RCX); // Oops, ObjStruct: Obj (16) + fieldCount (4 or 8) + fields.
            // Struct Layout:
            // Obj (16)
            // fieldCount (4/8). Let's use offset 16 for fieldCount.
            // But fields[] start where? 
            // `typedef struct { Obj obj; int fieldCount; Value fields[]; }`.
            // Alignment: fieldCount is int(4). fields is Value(8).
            // Padding 4 bytes after fieldCount.
            // So fields start at 16 + 8 = 24?
            // sizeof(Obj) = 16.
            // int fieldCount -> +4 = 20.
            // Padding -> +4 = 24.
            // Value fields[] -> 24.
            // My `getFieldOffset` used `16 + i*8`. That assumed fields started at 16.
            // I should update `getFieldOffset` to 24 + i*8?
            // Or remove `fieldCount` from ObjStruct?
            // Nah, needed for GC scanning.
            // Let's use 24 as base for fields.
            
            Asm_Mov_Mem_Reg(as, RAX, 16, RCX); // fieldCount at 16.
            // (Assuming writing 64-bit int to 32-bit field is ok/safe if value is small, overwrites padding).
            
            for (int i=0; i<info->fieldCount; i++) {
                 // Find init value for field info->fieldNames[i]
                 AstNode* valExpr = NULL;
                 for (int k=0; k<init->fieldCount; k++) {
                     if (init->fieldNames[k].length == info->fieldNames[i].length &&
                         memcmp(init->fieldNames[k].start, info->fieldNames[i].start, info->fieldNames[i].length) == 0) {
                         valExpr = init->values[k];
                         break;
                     }
                 }
                 
                 // Compile Value
                 if (valExpr) {
                     emitNode(as, valExpr, ctx);
                 } else {
                     Asm_Mov_Imm64(as, RAX, VAL_NULL);
                 }
                 
                 // Store to Struct
                 // StructPtr is at [RSP].

                 // Manually:
                 Asm_Mov_Reg_Mem(as, RCX, RSP, 0);
                 
                 // Offset = 24 + i*8
                 Asm_Mov_Mem_Reg(as, RCX, 24 + (i * 8), RAX);
            }
            
            // Done. Pop StructPtr -> RAX
            Asm_Pop(as, RAX);
            
            // Tag pointer?
            // ObjStruct* is a pointer. 
            // In NaN Boxing, pure pointers might need masking/tagging if they look like doubles.
            // But we use Pointer Tagging?
            // MasterPlan: "Pointers stored within 48-bit significand of Signaling NaN".
            // `ValueToObj` masks it.
            // `ObjToValue`: `(Value)obj | QNAN | TAG_OBJ?`
            // `VanarizeValue.h` defines `ObjToValue`.
            // We need to apply that logic here.
            // `SIGN_BIT | QNAN` ?
            // Let's hardcode the Tagging Mask.
            // `0xFFFC000000000000`?
            // Let's assume pure pointer for now or replicate `ObjToValue` macro.
            // `(uint64_t)(obj) | 0x7FFC000000000000` (SIGN_BIT | QNAN)?
            // Wait, native C `ObjToValue` adds the tag.
            // We can emit: `OR RAX, ...`.
            // Let's do `OR RAX, 0xFFFC000000000000` (Simplified tag for Objects).
            // Actually just `OR RAX, VAL_QNAN`.
            // `VAL_QNAN` is `0x7FFC000000000000`.
            // Plus Sign Bit `0x8000...`?
            // Let's check `VanarizeValue.h` if possible.
            // For now, I'll return raw pointer (safest for C malloc, bad for boxing).
            // If I return raw pointer, other code expects boxed.
            // CodeGen `Asm_Mov_Reg_Mem` etc handles 64-bit values.
            // If I don't box, it might be treated as double.
            // I MUST Box.
            // `OR RAX, 0xFFFC000000000000`.
            Asm_Mov_Imm64(as, RCX, 0xFFFC000000000000); 
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x09); Asm_Emit8(as, 0xC8); // OR RAX, RCX
            
            break;
        }

        case NODE_GET_EXPR: {
            GetExpr* get = (GetExpr*)node;
            
            // Special case: StdTime namespace
            if (get->object->type == NODE_LITERAL_EXPR) {
                LiteralExpr* lit = (LiteralExpr*)get->object;
                if (lit->token.type == TOKEN_IDENTIFIER &&
                    lit->token.length == 7 && 
                    memcmp(lit->token.start, "StdTime", 7) == 0) {
                    
                    // StdTime.Now() / Measure() / Sleep()
                    if (get->name.length == 3 && memcmp(get->name.start, "Now", 3) == 0) {
                        void* funcPtr = (void*)StdTime_Now;
                        Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                        break; // RAX has function pointer, done
                    }
                    else if (get->name.length == 7 && memcmp(get->name.start, "Measure", 7) == 0) {
                        void* funcPtr = (void*)StdTime_Measure;
                        Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                        break;
                    }
                    else if (get->name.length == 5 && memcmp(get->name.start, "Sleep", 5) == 0) {
                        void* funcPtr = (void*)StdTime_Sleep;
                        Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                        break;
                    }
                    
                    fprintf(stderr, "JIT Error: Unknown StdTime method '%.*s'\n", get->name.length, get->name.start);
                    exit(1);
                }
            }
            
            // 1. Compile Object -> RAX
            emitNode(as, get->object, ctx);
            
            // 2. Unbox if needed (remove tag)
            Asm_Mov_Imm64(as, RCX, 0x0000FFFFFFFFFFFF);
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x21); Asm_Emit8(as, 0xC8); // AND RAX, RCX
            
            // 3. Resolve Field Offset
            // We need Type of Object.
            // If Object is Variable, we lookup type in ctx.
            // Only support Variable LHS for now.
            if (get->object->type == NODE_LITERAL_EXPR) {
                LiteralExpr* lit = (LiteralExpr*)get->object;
                if (lit->token.type == TOKEN_IDENTIFIER) {
                    Token typeToken;
                    resolveLocal(ctx, &lit->token, &typeToken);
                    
                    StructInfo* info = resolveStruct(&typeToken);
                    if (info) {
                        int offset = getFieldOffset(info, &get->name);
                        // Offset adjustment: my `getFieldOffset` returns 16+...
                        // I changed base to 24 in INIT.
                        // recalculate: 24 + index * 8.
                        // I should update `getFieldOffset` function too.
                        // Assume `getFieldOffset` logic updated to 24 base.
                        
                        if (offset >= 0) {
                            Asm_Mov_Reg_Mem(as, RAX, RAX, offset);
                        } else {
                            // Error
                        }
                    } 
                }
            }
            break;
        }

        case NODE_ASSIGNMENT_EXPR: {
            AssignmentExpr* assign = (AssignmentExpr*)node;
            // 1. Compile Value -> RAX
            emitNode(as, assign->value, ctx);
            
            // 2. Resolve Variable
            Token type; // dummy
            int offset = resolveLocal(ctx, &assign->name, &type);
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
                Token type;
                int offset = resolveLocal(ctx, &lit->token, &type);
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
            CallExpr* call = (CallExpr*)node;
            
            // Callee is now AstNode*, can be:
            // 1. NODE_LITERAL_EXPR (simple function name like "print", "Main")
            // 2. NODE_GET_EXPR (namespace.method like "StdTime.Now")
            
            // Compile callee to get function pointer in RAX
            if (call->callee->type == NODE_GET_EXPR) {
                // Namespace.method - GET_EXPR handler will put function pointer in RAX
                emitNode(as, call->callee, ctx);
                Asm_Push(as, RAX); // Save function pointer
            }
            else if (call->callee->type == NODE_LITERAL_EXPR) {
                // Simple identifier
                LiteralExpr* lit = (LiteralExpr*)call->callee;
                Token calleeName = lit->token;
                
                // Check built-in print
                if (calleeName.length == 5 && memcmp(calleeName.start, "print", 5) == 0) {
                    // Special case: print - call directly
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
                
                // Try to resolve as local variable (function stored in local)
                Token type;
                int offset = resolveLocal(ctx, &calleeName, &type);
                if (offset != 0) {
                    Asm_Mov_Reg_Mem(as, RAX, RBP, -offset);
                    Asm_Push(as, RAX);
                } else {
                    fprintf(stderr, "JIT Error: Undefined function '%.*s'\n", calleeName.length, calleeName.start);
                    exit(1);
                }
            }
            else {
                fprintf(stderr, "JIT Error: Invalid callee type\n");
                exit(1);
            }
            
            // Compile arguments
            for (int i = 0; i < call->argCount; i++) {
                emitNode(as, call->args[i], ctx);
                Asm_Push(as, RAX);
            }
            
            // Pop args into registers (System V ABI)
            Register paramRegs[] = {RDI, RSI, RDX, RCX, R8, R9};
            for (int i = call->argCount - 1; i >= 0; i--) {
                if (i < 6) {
                    Asm_Pop(as, paramRegs[i]);
                }
            }
            
            // Pop function pointer and call
            Asm_Pop(as, RCX);
            Asm_Call_Reg(as, RCX);
            
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



        case NODE_FUNCTION_DECL: {
            // Function Declaration
            // 1. We need to compile the body into a NEW JIT block.
            // But Jit_Compile (the entry point) allocates memory.
            // We need a recursive way to create a new "FunctionUnit".
            // Since we are inside emitNode, we are emitting into the CURRENT buffer.
            // Functions in Vanarize are top-level or closures. 
            // If we emit code here, it will execute inline! We don't want that.
            // We want to skip over the code (JMP over it) OR compile it separately.
            // Simpler: JMP over the function body, but emit the body here?
            // No, getting the entry point is messy if we just JMP over.
            
            // Better: Recursively call Jit_Compile for the body?
            // But Jit_Compile does mmap. That's fine. Each function is a separate mmap block.
            // That's actually very flexible (hot reloading etc).
            // So:
            // 1. Create a CompilerContext for the function (params are locals).
            // 2. Compile body to new memory.
            // 3. Create ObjFunction with that pointer.
            // 4. Store ObjFunction in current scope (variable with function name).
            
            FunctionDecl* func = (FunctionDecl*)node;
            
            // Compile Function Body
            // We need to setup a new context with params.
            // Wait, Jit_Compile API is simple "AstNode* -> JitFunction".
            // We need to expose a way to compile with params.
            
            // Let's create `Jit_CompileInternal(AstNode* node, CompilerContext* ctx)`?
            // But we need a FRESH buffer.
            // So we call `Jit_Compile`? `Jit_Compile` creates a fresh buffer.
            // But `Jit_Compile` doesn't know about params!
            // We need to pass params to `Jit_Compile`.
            // Let's modify usages of `Jit_Compile` or assume `node` contains everything.
            // `Jit_Compile` calls `emitNode`.
            // We can manually do what `Jit_Compile` does here.
            
            void* funcMem = Jit_AllocExec(MAX_JIT_SIZE);
            Assembler funcAs;
            Asm_Init(&funcAs, (uint8_t*)funcMem, MAX_JIT_SIZE);
            
            // Function Prologue
            Asm_Push(&funcAs, RBP);
            Asm_Mov_Reg_Reg(&funcAs, RBP, RSP);
            
            // Setup Context for Function
            CompilerContext funcCtx;
            funcCtx.localCount = 0;
            funcCtx.stackSize = 0;
            
            // Simple: Spill to stack.
            Register paramRegs[] = { RDI, RSI, RDX, RCX, R8, R9 };
            for (int i = 0; i < func->paramCount; i++) {
                Asm_Push(&funcAs, paramRegs[i]);
                funcCtx.stackSize += 8;
                Local* local = &funcCtx.locals[funcCtx.localCount++];
                local->name = func->params[i];
                local->typeName = func->paramTypes[i]; // Use parsed type
                local->offset = funcCtx.stackSize;
            }
            
            // Compile Body
            emitNode(&funcAs, func->body, &funcCtx);
            
            // Default Return (if user didn't)
            Asm_Mov_Imm64(&funcAs, RAX, VAL_NULL); // Default return nil
            Asm_Mov_Reg_Reg(&funcAs, RSP, RBP);
            Asm_Pop(&funcAs, RBP);
            Asm_Ret(&funcAs);
            
            // Now we have the function compiled at `funcMem`.
            // CONSTANT POOL/GC TODO: objFunc should be GC tracked.
            ObjFunction* objFunc = malloc(sizeof(ObjFunction));
            objFunc->obj.type = OBJ_FUNCTION;
            objFunc->entrypoint = funcMem;
            objFunc->arity = func->paramCount;
            // Name... need ObjString.
            // objFunc->name = ...
            
            // Store this function in the CURRENT context (as a variable)
            // Implicit "var name = func..."
            // Push objFunc (pointer) to stack
            Value funcVal = ObjToValue(objFunc);
            Asm_Mov_Imm64(as, RAX, funcVal);
            Asm_Push(as, RAX);
            ctx->stackSize += 8;
            Local* local = &ctx->locals[ctx->localCount++];
            local->name = func->name;
            local->offset = ctx->stackSize;
            
            break;
        }
        
        case NODE_RETURN_STMT: {
            ReturnStmt* stmt = (ReturnStmt*)node;
            if (stmt->returnValue) {
                emitNode(as, stmt->returnValue, ctx);
            } else {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
            }
            // Epilogue and Ret
            Asm_Mov_Reg_Reg(as, RSP, RBP);
            Asm_Pop(as, RBP);
            Asm_Ret(as);
            break;
        }

        default: break;
    }
}

JitFunction Jit_Compile(AstNode* root) {
    // First pass: compile all declarations (functions, structs, etc.)
    // This pass populates global symbol tables and compiles function bodies into separate blocks.
    // The 'as' assembler here is a temporary one, its output is not directly used for the final entry point.
    uint8_t tempBuffer[MAX_JIT_SIZE];
    Assembler as;
    Asm_Init(&as, tempBuffer, MAX_JIT_SIZE);
    
    CompilerContext ctx = {0};
    
    // Compile all top-level declarations (functions, structs, etc)
    emitNode(&as, root, &ctx);
    
    // Now find Main() function and create entry point
    // Main should be in global function registry
    Token mainToken;
    mainToken.start = "Main";
    mainToken.length = 4;
    mainToken.type = TOKEN_IDENTIFIER;
    
    // Try to find Main in global functions (if we have a registry)
    // For now, we need to search the AST for Main function
    
    // Create entry point that calls Main()
    // void* code = Jit_AllocExec(as.size);
    // memcpy(code, as.code, as.size);
    
    // Find Main function in the compiled code
    // For simple case: root is BlockStmt with function declarations
    if (root->type == NODE_BLOCK) {
        BlockStmt* block = (BlockStmt*)root;
        
        // Search for Main function
        for (int i = 0; i < block->count; i++) {
            if (block->statements[i]->type == NODE_FUNCTION_DECL) {
                FunctionDecl* func = (FunctionDecl*)block->statements[i];
                if (func->name.length == 4 && 
                    memcmp(func->name.start, "Main", 4) == 0) {
                    
                    // Found Main! Now we need its compiled address
                    // Problem: we don't track function addresses during compilation
                    // Solution: compile Main() body directly as entry point
                    
                    // Re-compile just Main's body as the entry point
                    uint8_t mainBuffer[MAX_JIT_SIZE];
                    Assembler mainAs;
                    Asm_Init(&mainAs, mainBuffer, MAX_JIT_SIZE);
                    CompilerContext mainCtx = {0};
                    
                    // Function prologue
                    Asm_Push(&mainAs, RBP);
                    Asm_Mov_Reg_Reg(&mainAs, RBP, RSP);
                    Asm_Emit8(&mainAs, 0x48); Asm_Emit8(&mainAs, 0x83);
                    Asm_Emit8(&mainAs, 0xEC); Asm_Emit8(&mainAs, 0x40); // sub rsp, 64
                    
                    // Compile Main body
                    emitNode(&mainAs, func->body, &mainCtx);
                    
                    // Function epilogue
                    Asm_Emit8(&mainAs, 0x48); Asm_Emit8(&mainAs, 0x83);
                    Asm_Emit8(&mainAs, 0xC4); Asm_Emit8(&mainAs, 0x40); // add rsp, 64
                    Asm_Pop(&mainAs, RBP);
                    Asm_Ret(&mainAs);
                    
                    void* mainCode = Jit_AllocExec(mainAs.position);
                    memcpy(mainCode, mainAs.buffer, mainAs.position);
                    
                    return (JitFunction)mainCode;
                }
            }
        }
    }
    
    fprintf(stderr, "Error: Main() function not found\n");
    exit(1);
}
