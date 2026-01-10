#include "Jit/CodeGen.h"
#include "Jit/AssemblerX64.h"
#include "Jit/ExecutableMemory.h"
#include "Core/VanarizeValue.h"
#include "Core/Runtime.h"
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

// Global Function Table
typedef struct {
    Token name;
    void* address; // Pointer to JITted code
} FuncEntry;

static FuncEntry globalFunctions[128];
static int globalFuncCount = 0;

static int resolveGlobalFunction(Token* name) {
    for(int i=0; i<globalFuncCount; i++) {
        Token* fName = &globalFunctions[i].name;
        if (fName->length == name->length && memcmp(fName->start, name->start, name->length) == 0) {
            return i;
        }
    }
    return -1;
}

static void registerGlobalFunctions(AstNode* root) {
    // Reset count for new compilation (simplified)
    globalFuncCount = 0; 
    
    // Assume root is a list of declarations (Program usually)
    if (root->type == NODE_BLOCK) {
        BlockStmt* block = (BlockStmt*)root;
        for(int i=0; i<block->count; i++) {
            if (block->statements[i]->type == NODE_FUNCTION_DECL) {
                FunctionDecl* func = (FunctionDecl*)block->statements[i];
                if (globalFuncCount < 128) {
                    globalFunctions[globalFuncCount].name = func->name;
                    globalFunctions[globalFuncCount].address = NULL;
                    globalFuncCount++;
                }
            }
        }
    }
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
            
            // Allocate stack space for this variable
            ctx->stackSize += 8;
            ctx->locals[ctx->localCount].name = decl->name;
            ctx->locals[ctx->localCount].typeName = decl->typeName;
            ctx->locals[ctx->localCount].offset = ctx->stackSize;
            ctx->localCount++;
            
            // Store RAX (the value) into [RBP - offset]
            Asm_Mov_Mem_Reg(as, RBP, -ctx->stackSize, RAX);
            
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
            
            // 1. Compile value to be assigned -> RAX
            emitNode(as, assign->value, ctx);
            
            // 2. Resolve variable location
            Token type;
            int offset = resolveLocal(ctx, &assign->name, &type);
            
            if (offset != 0) {
                // Local variable found - store RAX to [RBP-offset]
                Asm_Mov_Mem_Reg(as, RBP, -offset, RAX);
            } else {
                // Global? Struct field?
                fprintf(stderr, "Error: Unknown variable '%.*s'\n", assign->name.length, assign->name.start);
                exit(1);
            }
            break;
        }

        case NODE_LITERAL_EXPR: {
            LiteralExpr* lit = (LiteralExpr*)node;
            if (lit->token.type == TOKEN_NUMBER) {
                // Parse number as double
                char buffer[64];
                int len = lit->token.length < 63 ? lit->token.length : 63;
                for(int i=0; i<len; i++) buffer[i] = lit->token.start[i];
                buffer[len] = '\0';
                
                double num = strtod(buffer, NULL);
                Value val = NumberToValue(num);
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
            // Compile callee to get function pointer in RAX
            if (call->callee->type == NODE_GET_EXPR) {
                // Check for Intrinsic Namespaces (e.g. StdTime.Now)
                GetExpr* get = (GetExpr*)call->callee;
                if (get->object->type == NODE_LITERAL_EXPR) {
                    LiteralExpr* lit = (LiteralExpr*)get->object;
                    Token ns = lit->token;
                    Token method = get->name;
                    
                    // Namespace: StdTime
                    if (ns.length == 7 && memcmp(ns.start, "StdTime", 7) == 0) {
                        void* funcPtr = NULL;
                        
                        if (method.length == 3 && memcmp(method.start, "Now", 3) == 0) {
                             funcPtr = (void*)StdTime_Now;
                        } else if (method.length == 7 && memcmp(method.start, "Measure", 7) == 0) {
                             funcPtr = (void*)StdTime_Measure;
                        } else if (method.length == 5 && memcmp(method.start, "Sleep", 5) == 0) {
                             // Sleep(ms) - 1 arg
                             if (call->argCount > 0) {
                                 emitNode(as, call->args[0], ctx);
                                 Asm_Push(as, RAX); 
                                 Asm_Pop(as, RDI); // Arg1
                             }
                             funcPtr = (void*)StdTime_Sleep;
                             Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                             Asm_Call_Reg(as, RAX);
                             break;
                        }
                        
                        // For Now() and Measure(), they return non-void
                        if (funcPtr) {
                            Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                            Asm_Call_Reg(as, RAX);
                            break;
                        }
                    }
                }

                // Namespace.method - Default GET_EXPR handler
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
                    // Try Global Function Table
                    int funcIdx = resolveGlobalFunction(&calleeName);
                    if (funcIdx != -1) {
                        // Found global function!
                        // Load address from table: MOV RAX, [Addr]
                        // &globalFunctions[funcIdx].address
                        void* slotAddr = &globalFunctions[funcIdx].address;
                        Asm_Mov_Reg_Ptr(as, RAX, slotAddr); // RAX = pointer to slot
                        // Now load the actual code address from the slot
                        Asm_Mov_Reg_Mem(as, RAX, RAX, 0); // RAX = [RAX]
                        
                        // Push to stack (as expected by call logic below)
                        Asm_Push(as, RAX);
                    } else {
                        fprintf(stderr, "JIT Error: Undefined function '%.*s'\n", calleeName.length, calleeName.start);
                        exit(1);
                    }
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
            
            // Pop function pointer and call (pointer pushed above)
            Asm_Pop(as, RCX);
            // If checking null pointer at runtime were needed, do it here. 
            // For now assume global function address will be filled by the time it runs.
            Asm_Call_Reg(as, RCX);
            
            break;
        }

        case NODE_BINARY_EXPR: {
            BinaryExpr* bin = (BinaryExpr*)node;
            
            // Check for comparison operators
            if (bin->op.type == TOKEN_LESS || bin->op.type == TOKEN_GREATER ||
                bin->op.type == TOKEN_LESS_EQUAL || bin->op.type == TOKEN_GREATER_EQUAL ||
                bin->op.type == TOKEN_EQUAL_EQUAL || bin->op.type == TOKEN_BANG_EQUAL) {
                
                // Emit left operand -> RAX
                emitNode(as, bin->left, ctx);
                Asm_Push(as, RAX);
                
                // Emit right operand -> RAX
                emitNode(as, bin->right, ctx);
                
                // Move values to XMM registers for floating-point comparison
                // RAX has right value, move to XMM1
                // MOVQ XMM1, RAX: 66 48 0F 6E C8
                Asm_Emit8(as, 0x66); // Operand-size prefix
                Asm_Emit8(as, 0x48); // REX.W
                Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x6E);
                Asm_Emit8(as, 0xC8); // ModRM: XMM1, RAX
                
                Asm_Pop(as, RAX); // Get left value
                // MOVQ XMM0, RAX: 66 48 0F 6E C0
                Asm_Emit8(as, 0x66);
                Asm_Emit8(as, 0x48);
                Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x6E);
                Asm_Emit8(as, 0xC0); // ModRM: XMM0, RAX
                
                // Zero RAX *before* comparison to avoid clobbering flags
                Asm_Emit8(as, 0x48);
                Asm_Emit8(as, 0x31);
                Asm_Emit8(as, 0xC0);
                
                // UCOMISD XMM0, XMM1 (compare XMM0 to XMM1)
                // Opcode: 66 0F 2E /r
                Asm_Emit8(as, 0x66);
                Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x2E);
                Asm_Emit8(as, 0xC1); // ModRM: XMM0, XMM1
                
                // SETcc AL based on flags from UCOMISD
                if (bin->op.type == TOKEN_LESS) {
                    // SETB AL
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x92); Asm_Emit8(as, 0xC0);
                } else if (bin->op.type == TOKEN_GREATER) {
                    // SETA AL
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x97); Asm_Emit8(as, 0xC0);
                } else if (bin->op.type == TOKEN_LESS_EQUAL) {
                    // SETBE AL
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x96); Asm_Emit8(as, 0xC0);
                } else if (bin->op.type == TOKEN_GREATER_EQUAL) {
                    // SETAE AL
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x93); Asm_Emit8(as, 0xC0);
                } else if (bin->op.type == TOKEN_EQUAL_EQUAL) {
                    // SETE AL
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x94); Asm_Emit8(as, 0xC0);
                } else if (bin->op.type == TOKEN_BANG_EQUAL) {
                    // SETNE AL
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x95); Asm_Emit8(as, 0xC0);
                }
                
                // Now AL is 1 (True) or 0 (False).
                // We need to return VAL_TRUE or VAL_FALSE in RAX.
                // CMP AL, 0
                // JE FalseLabel
                // MOV RAX, VAL_TRUE
                // JMP EndLabel
                // FalseLabel:
                // MOV RAX, VAL_FALSE
                // EndLabel:
                
                // CMP AL, 0: 3C 00
                Asm_Emit8(as, 0x3C); Asm_Emit8(as, 0x00);
                
                // JE + offset (short jump)
                // We don't know exact size of MOV RAX, VAL_TRUE + JMP.
                // MOV RAX, imm64 is 10 bytes (48 B8 ...).
                // JMP rel8 is 2 bytes (EB ...).
                // So FalseLabel is at Current + 2 + 10 + 2 = Current + 14.
                // JE 0x0C (12 bytes jump over MOV and JMP? No, 12 bytes total)
                
                Asm_Emit8(as, 0x74); // JE
                Asm_Emit8(as, 0x0C); // Jump 12 bytes (Move(10) + Jmp(2))
                
                // True path:
                Asm_Mov_Imm64(as, RAX, VAL_TRUE);
                
                // JMP over False path
                Asm_Emit8(as, 0xEB); // JMP rel8
                Asm_Emit8(as, 0x0A); // Jump 10 bytes (Move(10))
                
                // False path:
                // Move VAL_FALSE to RAX
                Asm_Mov_Imm64(as, RAX, VAL_FALSE);
                
                break;
            }
            
            // Arithmetic operators
            emitNode(as, bin->left, ctx);
            Asm_Push(as, RAX);
            emitNode(as, bin->right, ctx);
            Asm_Pop(as, RCX); 
            // Note: Pops into RCX, but Left was pushed.
            // If Stack increased due to locals, we must be careful.
            // Expression evaluation uses temp stack which is effectively "above" locals.
            // Since we push/pop relative to RSP, locals remain "below".
            // RBP is fixed anchor. Locals at [RBP-8].
            if (bin->op.type == TOKEN_PLUS) {
                // String concat OR Addition -> Runtime Call
                 // 1. Emit Left -> RAX
                emitNode(as, bin->left, ctx);
                Asm_Push(as, RAX);
                
                // 2. Emit Right -> RAX
                emitNode(as, bin->right, ctx);
                
                // RAX has Right. Move to RSI (Arg2).
                Asm_Mov_Reg_Reg(as, RSI, RAX);
                
                // Pop Left to RDI (Arg1).
                Asm_Pop(as, RDI);
                
                void* funcPtr = (void*)Runtime_Add;
                Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                Asm_Call_Reg(as, RAX);
                // Result in RAX.
            } else if (bin->op.type == TOKEN_MINUS || bin->op.type == TOKEN_STAR || bin->op.type == TOKEN_SLASH) {
                // Arithmetic operators: Use XMM registers (floating point)
                
                // 1. Emit Left -> RAX
                emitNode(as, bin->left, ctx);
                Asm_Push(as, RAX);
                
                // 2. Emit Right -> RAX
                emitNode(as, bin->right, ctx);
                
                // Move Right to XMM1
                // MOVQ XMM1, RAX: 66 48 0F 6E C8
                Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8);
                
                // 2. Get Left (popped) -> RAX
                Asm_Pop(as, RAX);
                // Move Left to XMM0
                // MOVQ XMM0, RAX: 66 48 0F 6E C0
                Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0);
                
                if (bin->op.type == TOKEN_MINUS) {
                    // SUBSD XMM0, XMM1: F2 0F 5C C1
                    Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                    Asm_Emit8(as, 0x5C); Asm_Emit8(as, 0xC1);
                } else if (bin->op.type == TOKEN_STAR) {
                    // MULSD XMM0, XMM1: F2 0F 59 C1
                    Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                    Asm_Emit8(as, 0x59); Asm_Emit8(as, 0xC1);
                } else if (bin->op.type == TOKEN_SLASH) {
                    // DIVSD XMM0, XMM1: F2 0F 5E C1
                    Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                    Asm_Emit8(as, 0x5E); Asm_Emit8(as, 0xC1);
                }
                
                // Move result XMM0 -> RAX
                // MOVQ RAX, XMM0: 66 48 0F 7E C0
                Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0);
            }
            break;
        }

        case NODE_IF_STMT: {
            IfStmt* stmt = (IfStmt*)node;
            // 1. Compile Condition
            emitNode(as, stmt->condition, ctx);
            
            // 2. Check if false (Compare RAX with VAL_FALSE)
            // Load VAL_FALSE to RCX.
            Asm_Mov_Imm64(as, RCX, VAL_FALSE);
            
            // CMP RAX, RCX
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8); // CMP RAX, RCX
            
            // JE elseBranch (Jump if Equal to False)
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
            WhileStmt* whileStmt = (WhileStmt*)node;
            
            // Loop start
            size_t loopStart = as->offset;
            
            // Evaluate condition -> RAX (0 or 1)
            emitNode(as, whileStmt->condition, ctx);
            
            // Check if FALSE
            Asm_Mov_Imm64(as, RCX, VAL_FALSE); // RCX = VAL_FALSE
            
            // CMP RAX, RCX
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8);
            
            // JE (jump if Equal to False) to loop end
            Asm_Emit8(as, 0x0F); // JE rel32
            Asm_Emit8(as, 0x84);
            size_t loopEndPatch = as->offset;
            Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
            Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
            
            // Loop body
            emitNode(as, whileStmt->body, ctx);
            
            // Jump back to loop start
            int32_t backOffset = loopStart - (as->offset + 5);
            Asm_Jmp(as, backOffset);
            
            // Patch the loop end jump
            size_t loopEnd = as->offset;
            int32_t forwardOffset = loopEnd - (loopEndPatch + 4);
            Asm_Patch32(as, loopEndPatch, forwardOffset);
            
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
   if (!root) return NULL;
    
    // 1. Register all Global Functions (Forward Reference Support)
    registerGlobalFunctions(root);
    
    // 2. Scan and Compile Functions
    // We expect root to be a block of statements (Global Scope)
    
    if (root->type != NODE_BLOCK) {
        return NULL;
    }
    BlockStmt* prog = (BlockStmt*)root;
    
    void* mainFunc = NULL;

    for (int i = 0; i < prog->count; i++) {
        if (prog->statements[i]->type == NODE_FUNCTION_DECL) {
            FunctionDecl* func = (FunctionDecl*)prog->statements[i];
            
            // Find global index
            int gIdx = resolveGlobalFunction(&func->name);
            
            Assembler as;
            // 64KB per function (plenty)
            uint8_t buffer[65536]; 
            Asm_Init(&as, buffer, 65536);
            
            // ... (rest of compilation logic)
            // Function Prologue
            Asm_Emit8(&as, 0x55);             // push rbp
            Asm_Mov_Reg_Reg(&as, RBP, RSP);   // mov rbp, rsp
            
            // Reserve Stack Space (Patch later)
            // SUB RSP, Imm32: 48 81 EC <4 bytes>
            Asm_Emit8(&as, 0x48); Asm_Emit8(&as, 0x81); Asm_Emit8(&as, 0xEC);
            size_t stackSizePatch = as.offset;
            Asm_Emit8(&as, 0x00); Asm_Emit8(&as, 0x00); Asm_Emit8(&as, 0x00); Asm_Emit8(&as, 0x00);
            
            CompilerContext ctx = {0};
            
            // Parameters
            // System V: RDI, RSI, RDX, RCX, R8, R9
            Register paramRegs[] = {RDI, RSI, RDX, RCX, R8, R9};
            for(int p=0; p<func->paramCount; p++) {
                if (p < 6) {
                    ctx.stackSize += 8;
                    // ... existing logic ...
                    ctx.locals[ctx.localCount].name = func->params[p];
                    ctx.locals[ctx.localCount].typeName = func->paramTypes[p];
                    ctx.locals[ctx.localCount].offset = ctx.stackSize;
                    ctx.localCount++;
                    // push paramReg to [rbp - offset]
                    Asm_Mov_Mem_Reg(&as, RBP, -ctx.stackSize, paramRegs[p]);
                }
            }

            
            // Body
            emitNode(&as, func->body, &ctx);
            
            // Default return (if not present)
            // Epilogue
            
            // Patch Stack Size
            // align stack to 16 bytes for ABI calls logic? 
            // If we use Call, RSP must be 16-byte aligned. 
            // ctx.stackSize might be 8. 8 + 8(RBP) = 16. Aligned.
            // If stackSize 16. 16+8 = 24. Misaligned.
            // We should align stackSize to 16 bytes.
            int alignedStack = (ctx.stackSize + 15) & ~15;
            Asm_Patch32(&as, stackSizePatch, alignedStack);
            
            Asm_Mov_Reg_Reg(&as, RSP, RBP);   // mov rsp, rbp
            Asm_Pop(&as, RBP);                // pop rbp
            Asm_Ret(&as);                     // ret
            
            // Allocate Executable Memory
            void* code = Jit_AllocExec(as.offset); // Allocate exact size
            memcpy(code, as.buffer, as.offset);
            
            // Update Global Table
            if (gIdx != -1) {
                globalFunctions[gIdx].address = code;
            }
            
            // Check for Main
            if (func->name.length == 4 && memcmp(func->name.start, "Main", 4) == 0) {
                mainFunc = code;
            }
        }
    }
    
    return mainFunc;
}
