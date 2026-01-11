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
#include "StdLib/StdMath.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>  // For floor() in integer detection

#define MAX_JIT_SIZE 4096

// Internal value type tracking for JIT optimization (Java types)
typedef enum {
    TYPE_UNKNOWN,
    TYPE_BYTE,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_CHAR,
    TYPE_BOOLEAN,
    TYPE_STRING
} ValueType;

// Simple Symbol Table for Locals
typedef struct {
    Token name;
    Token typeName;      // User-facing type (always "number")
    int offset;          // RBP offset (negative)
    int reg;             // -1 if stack, 0-4 for RBX, R12-R15
    ValueType internalType;  // INT64 vs DOUBLE (for specialization)
} Local;

typedef struct {
    Local locals[64];
    int localCount;
    int stackSize;
    int usedRegisters;      // Count of allocated registers (0-5)
    ValueType lastExprType; // Track type of last emitted expression
    int lastResultReg;      // Track which register holds last result
} CompilerContext;

// Struct Registry
typedef struct {
    Token name;
    Token fieldNames[32];
    int fieldCount;
} StructInfo;

static StructInfo globalStructs[64];
static int globalStructCount = 0;

static StructInfo* resolveStruct(Token* name) {
    for (int i = 0; i < globalStructCount; i++) {
        Token* sName = &globalStructs[i].name;
        if (sName->length == name->length && 
            memcmp(sName->start, name->start, name->length) == 0) {
            return &globalStructs[i];
        }
    }
    return NULL;
}

static void registerGlobalStructs(AstNode* root) {
    if (root->type == NODE_BLOCK) {
        BlockStmt* block = (BlockStmt*)root;
        for(int i=0; i<block->count; i++) {
            if (block->statements[i]->type == NODE_STRUCT_DECL) {
                StructDecl* decl = (StructDecl*)block->statements[i];
                if (globalStructCount < 64) {
                    StructInfo* info = &globalStructs[globalStructCount++];
                    info->name = decl->name;
                    info->fieldCount = decl->fieldCount;
                    for(int j=0; j<decl->fieldCount; j++) {
                        info->fieldNames[j] = decl->fields[j];
                    }
                }
            }
        }
    }
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

static int resolveLocal(CompilerContext* ctx, Token* name, Token* outType, int* outReg) {
    // Scan backwards to support shadowing
    for (int i = ctx->localCount - 1; i >= 0; i--) {
        Token* localName = &ctx->locals[i].name;
        if (localName->length == name->length && 
            memcmp(localName->start, name->start, name->length) == 0) {
            if (outType) *outType = ctx->locals[i].typeName;
            if (outReg) *outReg = ctx->locals[i].reg;
            return ctx->locals[i].offset;
        }
    }
    return 0; // Not found
}

static int allocRegister(CompilerContext* ctx) {
    if (ctx->usedRegisters < 5) {
        return ctx->usedRegisters++;
    }
    return -1;
}

// Helper to check if node results in a Number guaranteed
static int isGuaranteedNumber(AstNode* node, CompilerContext* ctx) {
    if (node->type == NODE_LITERAL_EXPR) {
        LiteralExpr* lit = (LiteralExpr*)node;
        if (lit->token.type == TOKEN_NUMBER) return 1;
        if (lit->token.type == TOKEN_IDENTIFIER) {
            Token type = {0};
            if (resolveLocal(ctx, &lit->token, &type, NULL)) {
                // Check if type token is 'number'
                if (type.length == 6 && memcmp(type.start, "number", 6) == 0) return 1;
            }
        }
    }
    // Todo: Binary Expr result is Number? (e.g. 1 + 2)
    return 0;
}

// Helper to check if node is guaranteed to be an INTEGER (no fractional part)
// NOTE: Will be used in Phase 3+ for safe integer specialization
static int __attribute__((unused)) isGuaranteedInteger(AstNode* node, CompilerContext* ctx) {
    if (node->type == NODE_LITERAL_EXPR) {
        LiteralExpr* lit = (LiteralExpr*)node;
        
        // Check if identifier references INT64 variable
        if (lit->token.type == TOKEN_IDENTIFIER) {
            for (int i = ctx->localCount - 1; i >= 0; i--) {
                Token* localName = &ctx->locals[i].name;
                if (localName->length == lit->token.length &&
                    memcmp(localName->start, lit->token.start, lit->token.length) == 0) {
                    return ctx->locals[i].internalType == TYPE_INT64;
                }
            }
            return 0;
        }
        
        // Check if numeric literal is whole number
        if (lit->token.type == TOKEN_NUMBER) {
            char buffer[64];
            int len = lit->token.length < 63 ? lit->token.length : 63;
            for (int i = 0; i < len; i++) buffer[i] = lit->token.start[i];
            buffer[len] = '\0';
            
            double val = strtod(buffer, NULL);
            // Check if value is whole and within int64 range
            return (floor(val) == val && val >= (double)INT64_MIN && val <= (double)INT64_MAX);
        }
    }
    
    // Binary expressions: both operands must be int64
    if (node->type == NODE_BINARY_EXPR) {
        BinaryExpr* bin = (BinaryExpr*)node;
        return isGuaranteedInteger(bin->left, ctx) && isGuaranteedInteger(bin->right, ctx);
    }
    
    return 0;
}

static void emitRegisterMove(Assembler* as, int regIndex, int srcReg) {
    // Move srcReg (usually RAX) to Dest Reg (RBX, R12-R15)
    // Reg Map: 0:RBX, 1:R12, 2:R13, 3:R14, 4:R15
    switch (regIndex) {
        case 0: Asm_Mov_Reg_Reg(as, RBX, srcReg); break;
        case 1: Asm_Mov_Reg_Reg(as, R12, srcReg); break;
        case 2: Asm_Mov_Reg_Reg(as, R13, srcReg); break;
        case 3: Asm_Mov_Reg_Reg(as, R14, srcReg); break;
        case 4: Asm_Mov_Reg_Reg(as, R15, srcReg); break;
    }
}

static void emitRegisterLoad(Assembler* as, int dstReg, int regIndex) {
    // Move from Src Reg (RBX...) to Dst Reg (usually RAX)
    switch (regIndex) {
        case 0: Asm_Mov_Reg_Reg(as, dstReg, RBX); break;
        case 1: Asm_Mov_Reg_Reg(as, dstReg, R12); break;
        case 2: Asm_Mov_Reg_Reg(as, dstReg, R13); break;
        case 3: Asm_Mov_Reg_Reg(as, dstReg, R14); break;
        case 4: Asm_Mov_Reg_Reg(as, dstReg, R15); break;
    }
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
            
            // ==================== PHASE 0: UNIFIED VALUE STORAGE ====================
            // All numbers stored as NaN-boxed doubles for consistency
            
            // Add to Locals first (so we can detect type)
            Local* local = &ctx->locals[ctx->localCount++];
            local->name = decl->name;
            local->typeName = decl->typeName;
            local->reg = -1; // Default to stack
            local->internalType = TYPE_DOUBLE; // All numbers are doubles
            
            // Compile Init Value -> RAX
            if (decl->initializer) {
                if (decl->initializer->type == NODE_LITERAL_EXPR) {
                    LiteralExpr* lit = (LiteralExpr*)decl->initializer;
                    if (lit->token.type == TOKEN_NUMBER) {
                        // Parse number and store as NaN-boxed double
                        char buffer[64];
                        int len = lit->token.length < 63 ? lit->token.length : 63;
                        for (int i = 0; i < len; i++) buffer[i] = lit->token.start[i];
                        buffer[len] = '\0';
                        
                        double value = strtod(buffer, NULL);
                        Value val = NumberToValue(value);
                        Asm_Mov_Imm64(as, RAX, val);
                        ctx->lastExprType = TYPE_DOUBLE;
                    } else {
                        // Non-number literal (string, bool, etc.)
                        emitNode(as, decl->initializer, ctx);
                        local->internalType = TYPE_UNKNOWN;
                    }
                } else {
                    // Complex expression - emit normally
                    emitNode(as, decl->initializer, ctx);
                    local->internalType = ctx->lastExprType;
                }
            } else {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
                local->internalType = TYPE_UNKNOWN;
            }
            
            // Optimization: If number type, try allocate register
            if (decl->typeName.length == 6 && memcmp(decl->typeName.start, "number", 6) == 0) {
                 int reg = allocRegister(ctx);
                 if (reg != -1) {
                     local->reg = reg;
                     // Move RAX to Reg
                     emitRegisterMove(as, reg, RAX);
                    ctx->lastResultReg = reg;
                     // Set offset to 0 as it's not on stack
                     local->offset = 0; 
                     break; 
                 }
            }

            // Fallback: Stack
            Asm_Push(as, RAX);
            ctx->stackSize += 8;
            local->offset = ctx->stackSize;
            
            break;
        }

        case NODE_STRUCT_DECL: {
            // Register struct - no code to emit
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
            // Stack is misaligned by 8 bytes due to PUSH RAX
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xEC); Asm_Emit8(as, 0x08); // SUB RSP, 8
            
            Asm_Mov_Reg_Reg(as, RDI, RAX); // First arg = obj
            void* gcRegPtr = (void*)GC_RegisterObject;
            Asm_Mov_Reg_Ptr(as, RAX, gcRegPtr);
            Asm_Call_Reg(as, RAX);
            
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xC4); Asm_Emit8(as, 0x08); // ADD RSP, 8
            
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
                    Token typeToken = {0};
                    resolveLocal(ctx, &lit->token, &typeToken, NULL);
                    
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
            emitNode(as, assign->value, ctx);
            // Value in RAX, remember its type
            ValueType assignedType = ctx->lastExprType;
            
            Token type;
            int reg = -1;
            int offset = resolveLocal(ctx, &assign->name, &type, &reg);
            if (offset > 0 || reg != -1) {
                // Update local's type
                for (int i = ctx->localCount - 1; i >= 0; i--) {
                    if (ctx->locals[i].name.length == assign->name.length &&
                        memcmp(ctx->locals[i].name.start, assign->name.start, assign->name.length) == 0) {
                        ctx->locals[i].internalType = assignedType;
                        break;
                    }
                }
                
                if (reg != -1) {
                     // Store RAX to Reg
                     emitRegisterMove(as, reg, RAX);
                } else {
                     // Store RAX to [RBP - offset]
                     // offset is positive from ctx start.
                     // Local at [RBP - offset]
                     Asm_Mov_Mem_Reg(as, RBP, -offset, RAX);
                }
            } else {
                fprintf(stderr, "JIT Error: Assignment to unknown variable '%.*s'\n", assign->name.length, assign->name.start);
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
                int reg = -1; // Added for resolveLocal
                int offset = resolveLocal(ctx, &lit->token, &type, &reg); // Updated call
                if (offset > 0 || reg != -1) { // Updated condition
                    if (reg != -1) {
                        emitRegisterLoad(as, RAX, reg); // New
                    } else {
                        // Load from [RBP - offset] -> RAX
                        Asm_Mov_Reg_Mem(as, RAX, RBP, -offset);
                    }
                } else {
                     fprintf(stderr, "JIT Error: Undefined variable '%.*s'\n", lit->token.length, lit->token.start);
                     exit(1);
                 }
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
                    } else if (ns.length == 7 && memcmp(ns.start, "StdMath", 7) == 0) {
                        void* funcPtr = NULL;
                        // Single argument math functions
                        
                        if (method.length == 3 && memcmp(method.start, "Sin", 3) == 0) {
                             funcPtr = (void*)StdMath_Sin;
                        } else if (method.length == 3 && memcmp(method.start, "Cos", 3) == 0) {
                             funcPtr = (void*)StdMath_Cos;
                        } else if (method.length == 3 && memcmp(method.start, "Tan", 3) == 0) {
                             funcPtr = (void*)StdMath_Tan;
                        } else if (method.length == 4 && memcmp(method.start, "Sqrt", 4) == 0) {
                             funcPtr = (void*)StdMath_Sqrt;
                        } else if (method.length == 3 && memcmp(method.start, "Abs", 3) == 0) {
                             funcPtr = (void*)StdMath_Abs;
                        } else if (method.length == 5 && memcmp(method.start, "Floor", 5) == 0) {
                             funcPtr = (void*)StdMath_Floor;
                        } else if (method.length == 4 && memcmp(method.start, "Ceil", 4) == 0) {
                             funcPtr = (void*)StdMath_Ceil; 
                        }
                        
                        if (funcPtr) {
                            // 1 Arg
                            if (call->argCount > 0) {
                                emitNode(as, call->args[0], ctx);
                                Asm_Push(as, RAX); 
                                Asm_Pop(as, RDI); 
                            }
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
                int reg = -1;
                int offset = resolveLocal(ctx, &calleeName, &type, &reg);
                if (offset > 0 || reg != -1) {
                    if (reg != -1) {
                        emitRegisterLoad(as, RAX, reg);
                    } else {
                        Asm_Mov_Reg_Mem(as, RAX, RBP, -offset);
                    }
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
            
            // ==================== PHASE 0: UNIFIED DOUBLE ARITHMETIC ====================
            // All arithmetic uses NaN-boxed doubles for maximum stability.
            // Performance optimizations will be added in later phases.
            
            // Handle arithmetic operators: +, -, *, /
            if (bin->op.type == TOKEN_PLUS || bin->op.type == TOKEN_MINUS || 
                bin->op.type == TOKEN_STAR || bin->op.type == TOKEN_SLASH) {
                
                // Check if this is guaranteed to be number arithmetic (not string concat)
                int isNumericAdd = (bin->op.type != TOKEN_PLUS) || 
                                   (isGuaranteedNumber(bin->left, ctx) && isGuaranteedNumber(bin->right, ctx));
                
                if (isNumericAdd) {
                    // UNIFIED DOUBLE PATH: All values as NaN-boxed doubles
                    
                    // 1. Emit left operand -> RAX
                    emitNode(as, bin->left, ctx);
                    
                    // 2. Save left to R10 (temp register)
                    Asm_Mov_Reg_Reg(as, R10, RAX);
                    
                    // 3. Emit right operand -> RAX
                    emitNode(as, bin->right, ctx);
                    
                    // Now: R10 = left (NaN-boxed), RAX = right (NaN-boxed)
                    
                    // 4. Move right to XMM1: MOVQ XMM1, RAX
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F);
                    Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // MOVQ XMM1, RAX
                    
                    // 5. Move left to XMM0: MOVQ XMM0, R10 (REX.B for R10)
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x49); Asm_Emit8(as, 0x0F);
                    Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC2); // MOVQ XMM0, R10
                    
                    // 6. Perform double operation
                    switch (bin->op.type) {
                        case TOKEN_PLUS:
                            // ADDSD XMM0, XMM1: F2 0F 58 C1
                            Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                            Asm_Emit8(as, 0x58); Asm_Emit8(as, 0xC1);
                            break;
                        case TOKEN_MINUS:
                            // SUBSD XMM0, XMM1: F2 0F 5C C1
                            Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                            Asm_Emit8(as, 0x5C); Asm_Emit8(as, 0xC1);
                            break;
                        case TOKEN_STAR:
                            // MULSD XMM0, XMM1: F2 0F 59 C1
                            Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                            Asm_Emit8(as, 0x59); Asm_Emit8(as, 0xC1);
                            break;
                        case TOKEN_SLASH:
                            // DIVSD XMM0, XMM1: F2 0F 5E C1
                            Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F);
                            Asm_Emit8(as, 0x5E); Asm_Emit8(as, 0xC1);
                            break;
                        default:
                            break;
                    }
                    
                    // 7. Move result back to RAX: MOVQ RAX, XMM0
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F);
                    Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                    
                    // State update
                    ctx->lastExprType = TYPE_DOUBLE;
                    ctx->lastResultReg = -1;
                    
                    break; // Exit NODE_BINARY_EXPR
                }
            }
            
            // ==================== FALLBACK TO DOUBLE/COMPARISON PATH ====================
            
            // Check for comparison operators
            if (bin->op.type == TOKEN_LESS || bin->op.type == TOKEN_GREATER ||
                bin->op.type == TOKEN_LESS_EQUAL || bin->op.type == TOKEN_GREATER_EQUAL ||
                bin->op.type == TOKEN_EQUAL_EQUAL || bin->op.type == TOKEN_BANG_EQUAL) {
                
                // Emit left operand -> RAX
                emitNode(as, bin->left, ctx);
                
                int rightSimple = (bin->right->type == NODE_LITERAL_EXPR);
                if (rightSimple) {
                    Asm_Mov_Reg_Reg(as, R10, RAX); // Save Left to R10
                    emitNode(as, bin->right, ctx); // Emit Right -> RAX
                    // Left is in R10.
                    // Need Left in RCX? Or just use portions of logic below.
                    // Below logic expects Left popped to RAX, Right in XMM1?
                    // Wait, logic below:
                    // RAX has Right. Move to XMM1.
                    // Pop Left -> RAX. Move to XMM0.
                    
                    // We can adapt:
                    // Right is in RAX.
                    // Left is in R10.
                } else {
                    Asm_Push(as, RAX);
                    emitNode(as, bin->right, ctx);
                    Asm_Pop(as, R10); // Pop Left to R10 (unified register)
                }
                
                // Move values to XMM registers for floating-point comparison
                // RAX has right value, move to XMM1
                // MOVQ XMM1, RAX: 66 48 0F 6E C8
                Asm_Emit8(as, 0x66); // Operand-size prefix
                Asm_Emit8(as, 0x48); // REX.W
                Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x6E);
                Asm_Emit8(as, 0xC8); // ModRM: XMM1, RAX
                
                // Left is in R10.
                // MOVQ XMM0, R10: 66 48 0F 6E C2 (R10 is #2? No. R10=10 (0x0A). Need REX.B)
                // R10 is extended (needs REX.B=1). 
                // XMM0 is 0.
                // Opcode: 66 REX.W(48)|REX.B(1)? = 49?
                // MOVQ XMM, r64 uses REX.W. If r64 is extended, REX.B.
                // REX = 48 | (R10>=8?1:0) = 49.
                Asm_Emit8(as, 0x66);
                Asm_Emit8(as, 0x49); // REX.WB
                Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0x6E);
                Asm_Emit8(as, 0xC2); // ModRM: XMM0(0), R10(2 in low 3 bits? 10&7=2. Yes) -> C2.
                
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
                // Branchless conversion to VAL_TRUE / VAL_FALSE
                // VAL_FALSE = QNAN | 2
                // VAL_TRUE  = QNAN | 3 = VAL_FALSE + 1
                
                // MOVZX RAX, AL: 0F B6 C0 (Zero extend AL to RAX)
                Asm_Emit8(as, 0x0F);
                Asm_Emit8(as, 0xB6);
                Asm_Emit8(as, 0xC0);
                
                // MOV RCX, VAL_FALSE
                Asm_Mov_Imm64(as, RCX, VAL_FALSE);
                
                // ADD RAX, RCX: 48 01 C8
                Asm_Emit8(as, 0x48);
                Asm_Emit8(as, 0x01);
                Asm_Emit8(as, 0xC8);
                
                // Result in RAX is VAL_TRUE or VAL_FALSE. No branches.
                
                // No branches.
                
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
                // String concat OR Addition
                
                int safe = isGuaranteedNumber(bin->left, ctx) && isGuaranteedNumber(bin->right, ctx);
                
                if (safe) {
                    // BLIND FAST PATH (No Type Checks)
                    emitNode(as, bin->left, ctx);
                    
                    int rightSimple = (bin->right->type == NODE_LITERAL_EXPR);
                    
                    if (rightSimple) {
                        Asm_Mov_Reg_Reg(as, R10, RAX);
                        emitNode(as, bin->right, ctx);
                        // Left=R10, Right=RAX
                        // Target: RCX=Left for code below?
                        Asm_Mov_Reg_Reg(as, RCX, R10);
                    } else {
                        Asm_Push(as, RAX);
                        emitNode(as, bin->right, ctx);
                        Asm_Pop(as, RCX);
                    }
                    
                    // ADDSD XMM0 (Left=RCX), XMM1 (Right=RAX)
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC1); // MOVQ XMM0, RCX
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // MOVQ XMM1, RAX
                    Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x58); Asm_Emit8(as, 0xC1); // ADDSD XMM0, XMM1
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                } else {
                    // CHECKED PATH (Reg Promotion / Runtime Fallback)
                    // ... (Logic from previous step, kept for safety for non-numbers)
                    emitNode(as, bin->left, ctx);
                    Asm_Push(as, RAX);
                    emitNode(as, bin->right, ctx);
                    Asm_Pop(as, RCX);
                    
                    Asm_Mov_Imm64(as, R11, 0x7FFC000000000000);
                    
                    Asm_Mov_Reg_Reg(as, R10, RAX);
                    Asm_And_Reg_Reg(as, R10, R11);
                    Asm_Cmp_Reg_Reg(as, R10, R11);
                    size_t jumpSlow1 = as->offset + 2; 
                    Asm_Je(as, 0); 
                    
                    Asm_Mov_Reg_Reg(as, R10, RCX);
                    Asm_And_Reg_Reg(as, R10, R11);
                    Asm_Cmp_Reg_Reg(as, R10, R11);
                    size_t jumpSlow2 = as->offset + 2;
                    Asm_Je(as, 0);
                    
                    // Fast Add
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC1);
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8);
                    Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x58); Asm_Emit8(as, 0xC1);
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0);
                    
                    size_t jumpDone = as->offset + 1;
                    Asm_Jmp(as, 0);
                    
                    // Slow Path Call
                    size_t slowStart = as->offset;
                    Asm_Patch32(as, jumpSlow1, slowStart - (jumpSlow1 + 4));
                    Asm_Patch32(as, jumpSlow2, slowStart - (jumpSlow2 + 4));
                    
                    Asm_Mov_Reg_Reg(as, RDI, RCX);
                    Asm_Mov_Reg_Reg(as, RSI, RAX);
                    void* funcPtr = (void*)Runtime_Add;
                    Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                    Asm_Call_Reg(as, RAX);
                    
                    size_t doneStart = as->offset;
                    Asm_Patch32(as, jumpDone, doneStart - (jumpDone + 4));
                }
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

        case NODE_UNARY_EXPR: {
            UnaryExpr* unary = (UnaryExpr*)node;
            emitNode(as, unary->right, ctx);
            // RAX has operand
            
            if (unary->op.type == TOKEN_MINUS) {
                // Negate Number
                Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); // MOVQ XMM0, RAX
                Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x57); Asm_Emit8(as, 0xC9); // XORPS XMM1, XMM1 (0.0)
                Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5C); Asm_Emit8(as, 0xC8); // SUBSD XMM1, XMM0
                Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC8); // MOVQ RAX, XMM1
            } 
            else if (unary->op.type == TOKEN_BANG) {
                // Not (!)
                // Result in RAX is VAL_TRUE or VAL_FALSE. No branches.
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
            
            // OPTIMIZATION: Fused Compare-Branch
            int fused = 0;
            size_t loopEndPatch = 0;
            
            if (whileStmt->condition->type == NODE_BINARY_EXPR) {
                BinaryExpr* bin = (BinaryExpr*)whileStmt->condition;
                if (bin->op.type == TOKEN_LESS) { // Only optimizing '<' for now (common in loops)
                     // Emit Left -> RAX/R10
                     emitNode(as, bin->left, ctx);
                     int rightSimple = (bin->right->type == NODE_LITERAL_EXPR);
                     if (rightSimple) {
                         Asm_Mov_Reg_Reg(as, R10, RAX);
                         emitNode(as, bin->right, ctx);
                         // Left=R10, Right=RAX
                     } else {
                         Asm_Push(as, RAX);
                         emitNode(as, bin->right, ctx);
                         Asm_Pop(as, R10);
                     }
                     // Move to XMM
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // XMM1 = Right
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x49); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC2); // XMM0 = Left (R10)
                     
                     // UCOMISD XMM0, XMM1 (Left < Right)
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2E); Asm_Emit8(as, 0xC1);
                     
                     // Loop condition is "Run while True" (Left < Right).
                     // We jump to End if False (Left >= Right).
                     // UCOMISD sets CF=1 if Less.
                     // JAE (Jump if Above or Equal, CF=0) skips loop.
                     // JAE rel32: 0F 83
                     Asm_Emit8(as, 0x0F);
                     Asm_Emit8(as, 0x83);
                     loopEndPatch = as->offset;
                     Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
                     
                     fused = 1;
                }
            }
            
            if (!fused) {
                // Default: Evaluate condition -> RAX (0 or 1)
                emitNode(as, whileStmt->condition, ctx);
                
                // Check if FALSE
                Asm_Mov_Imm64(as, RCX, VAL_FALSE); // RCX = VAL_FALSE
                // CMP RAX, RCX
                Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8);
                // JE (jump if Equal to False) to loop end
                Asm_Emit8(as, 0x0F); // JE rel32
                Asm_Emit8(as, 0x84);
                loopEndPatch = as->offset;
                Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
                Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
            }
            
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
    
    // 1. Register Global Structs and Functions
    registerGlobalStructs(root);
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
            
            // Save Callee-Saved Registers (RBX, R12-R15)
            // 5 regs * 8 bytes = 40 bytes
            Asm_Push(&as, RBX);
            Asm_Push(&as, R12);
            Asm_Push(&as, R13);
            Asm_Push(&as, R14);
            Asm_Push(&as, R15);
            
            // Reserve Stack Space (Patch later)
            // REMOVED: SUB RSP breaks PUSH-based variable declaration pattern.
            // Asm_Emit8(&as, 0x48); Asm_Emit8(&as, 0x81); Asm_Emit8(&as, 0xEC);
            // size_t stackSizePatch = as.offset;
            // Asm_Emit8(&as, 0x00); Asm_Emit8(&as, 0x00); Asm_Emit8(&as, 0x00); Asm_Emit8(&as, 0x00);
            
            CompilerContext ctx = {0};
            ctx.stackSize = 40; // Reserved for saved regs
            
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
                    // Asm_Mov_Mem_Reg(&as, RBP, -ctx.stackSize, paramRegs[p]);
                    Asm_Push(&as, paramRegs[p]); // Use PUSH for consistency
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
            // Stack Alignment Rule: (SavedRegs + LocalStack) % 16 == 0
            // SavedRegs = 40 bytes.
            // 40 % 16 = 8. So we are misaligned by 8 bytes relative to 16-byte boundary (after PUSH RBP).
            // We need to subtract an amount 'S' such that (40 + S) % 16 == 0.
            
            int localSize = ctx.stackSize - 40;
            if (localSize < 0) localSize = 0;
            
            int totalPushed = 40 + localSize;
            int padding = (16 - (totalPushed % 16)) % 16;
            int finalStackSize __attribute__((unused)) = localSize + padding;
            
            // Write to SUB RSP, Imm32
            // REMOVED: No more backpatching.
            // uint32_t sizeVal = (uint32_t)finalStackSize;
            // memcpy(as.buffer + stackSizePatch, &sizeVal, 4);
            
            // Epilogue
            // Restore Stack: LEA RSP, [RBP - 40]
            // LEA RSP, [RBP - 40]: 48 8D 65 D8
            Asm_Emit8(&as, 0x48); Asm_Emit8(&as, 0x8D); Asm_Emit8(&as, 0x65); Asm_Emit8(&as, 0xD8);
            Asm_Emit8(&as, 0x48); Asm_Emit8(&as, 0x8D); Asm_Emit8(&as, 0x65); Asm_Emit8(&as, 0xD8);
            
            // Pop Regs (Reverse order)
            Asm_Pop(&as, R15);
            Asm_Pop(&as, R14);
            Asm_Pop(&as, R13);
            Asm_Pop(&as, R12);
            Asm_Pop(&as, RBX);
            
            // Restore RBP and Ret
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
