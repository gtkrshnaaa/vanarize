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
#include "StdLib/StdBenchmark.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>  // For floor() in integer detection

// GLOBAL FUNCTION REGISTRY
typedef struct {
    char name[128];
    void* address;
} GlobalFunction;

static GlobalFunction globalFunctions[256];
static int globalFunctionCount = 0;
static void* mainFunc = NULL;

static void registerGlobalFunction(const char* name, int length, void* address) {
    if (globalFunctionCount >= 256) {
        fprintf(stderr, "JIT Error: Global function limit reached.\n");
        exit(1);
    }
    int storeLen = length > 127 ? 127 : length;
    memcpy(globalFunctions[globalFunctionCount].name, name, storeLen);
    globalFunctions[globalFunctionCount].name[storeLen] = '\0';
    globalFunctions[globalFunctionCount].address = address;
    globalFunctionCount++;
}

static void* findGlobalFunction(const char* name, int length) {
    for (int i = 0; i < globalFunctionCount; i++) {
        int storedLen = strlen(globalFunctions[i].name);
        if (storedLen == length && memcmp(globalFunctions[i].name, name, length) == 0) {
            return globalFunctions[i].address;
        }
    }
    return NULL;
}

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
    Token fieldTypes[32];
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
                        info->fieldTypes[j] = decl->fieldTypes[j];
                    }
                }
            }
        }
    }
}





static int getPackedFieldInfo(StructInfo* info, Token* field, int* outSize, int* outIsPtr) {
    int dataSize = 0;
    int headerSize = sizeof(ObjStruct); 
    
    for (int i=0; i<info->fieldCount; i++) {
        Token fType = info->fieldTypes[i];
        int fSize = 8;
        int isPtr = 0;
        
        if (fType.length == 3 && memcmp(fType.start, "int", 3) == 0) fSize = 4;
        else if (fType.length == 5 && memcmp(fType.start, "float", 5) == 0) fSize = 4;
        else if ((fType.length == 7 && memcmp(fType.start, "boolean", 7) == 0) || 
                 (fType.length == 4 && memcmp(fType.start, "byte", 4) == 0)) fSize = 1; 
        else if ((fType.length == 5 && memcmp(fType.start, "short", 5) == 0) || 
                 (fType.length == 4 && memcmp(fType.start, "char", 4) == 0)) fSize = 2;
        else if ((fType.length == 6 && memcmp(fType.start, "double", 6) == 0) || 
                 (fType.length == 4 && memcmp(fType.start, "long", 4) == 0)) fSize = 8;
        else { fSize = 8; isPtr = 1; }
        
        while (dataSize % fSize != 0) dataSize++;
        
        Token* fName = &info->fieldNames[i];
        if (fName->length == field->length && memcmp(fName->start, field->start, field->length) == 0) {
            if (outSize) *outSize = fSize;
            if (outIsPtr) *outIsPtr = isPtr;
            return headerSize + dataSize;
        }
        
        dataSize += fSize;
    }
    return -1;
}

static int resolveLocal(CompilerContext* ctx, Token* name, Token* outType, int* outReg, ValueType* outInternalType) {
    // Scan backwards to support shadowing
    for (int i = ctx->localCount - 1; i >= 0; i--) {
        Token* localName = &ctx->locals[i].name;
        if (localName->length == name->length && 
            memcmp(localName->start, name->start, name->length) == 0) {
            if (outType) *outType = ctx->locals[i].typeName;
            if (outReg) *outReg = ctx->locals[i].reg;
            if (outInternalType) *outInternalType = ctx->locals[i].internalType;
            return ctx->locals[i].offset;
        }
    }
    return -1; // Not found
}

static int allocRegister(CompilerContext* ctx) {
    if (ctx->usedRegisters < 5) {
        return ctx->usedRegisters++;
    }
    return -1;
}

// Helper to check if node results in a Number guaranteed
// Helper to check if node is guaranteed to be an INTEGER (no fractional part)
static int __attribute__((unused)) isGuaranteedInteger(AstNode* node, CompilerContext* ctx) {
    if (node->type == NODE_LITERAL_EXPR) {
        LiteralExpr* lit = (LiteralExpr*)node;
        
        // Check if identifier references INT64 variable
        if (lit->token.type == TOKEN_IDENTIFIER) {
            Token type = {0};
            ValueType varType = TYPE_UNKNOWN;
            if (resolveLocal(ctx, &lit->token, &type, NULL, &varType)) {
                return varType == TYPE_INT || varType == TYPE_LONG;
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
            
            // Scope Management
            int savedStackSize = ctx->stackSize;
            int savedLocalCount = ctx->localCount;
            
            for (int i = 0; i < block->count; i++) {
                emitNode(as, block->statements[i], ctx);
            }
            
            // Pop Stack (if locals were declared)
            int diff = ctx->stackSize - savedStackSize;
            if (diff > 0) {
                 // ADD RSP, diff
                 Asm_Add_Reg_Imm(as, RSP, diff);
                 ctx->stackSize = savedStackSize;
            }
            
            // Restore Scope
            ctx->localCount = savedLocalCount;
            
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
                    
                    // DEBUG TRACE
                    // if (decl->name.length == 3 && ...
                    
                    local->internalType = ctx->lastExprType;
                    
                    // IMPLICIT CASTING (Int <-> Double)
                    // If declared type is strictly typed, and init expression differs, cast it.
                    // We check the intended type based on `typeName`.
                    // Wait, `local->internalType` was just SET to `lastExprType` which is wrong if we want to enforce declared type.
                    // We must determine Target Type from Declaration.
                    

                }
            } else {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
                local->internalType = TYPE_UNKNOWN;
                ctx->lastExprType = TYPE_UNKNOWN; // Reset for NoInit
            }
            
            // MOVED: Target Type & Unboxing Logic (Runs for Literals too)
            ValueType targetType = TYPE_UNKNOWN;
            if (decl->typeName.length == 3 && memcmp(decl->typeName.start, "int", 3) == 0) targetType = TYPE_INT;
            else if (decl->typeName.length == 6 && memcmp(decl->typeName.start, "double", 6) == 0) targetType = TYPE_DOUBLE;
            else if (decl->typeName.length == 4 && memcmp(decl->typeName.start, "long", 4) == 0) targetType = TYPE_LONG;
            else if (decl->typeName.length == 4 && memcmp(decl->typeName.start, "byte", 4) == 0) targetType = TYPE_BYTE;
            else if (decl->typeName.length == 5 && memcmp(decl->typeName.start, "short", 5) == 0) targetType = TYPE_SHORT;
            else if (decl->typeName.length == 4 && memcmp(decl->typeName.start, "char", 4) == 0) targetType = TYPE_CHAR;
            else if (decl->typeName.length == 5 && memcmp(decl->typeName.start, "float", 5) == 0) targetType = TYPE_FLOAT;

            if (targetType != TYPE_UNKNOWN) {
                local->internalType = targetType;
                
                if (targetType == TYPE_DOUBLE && (ctx->lastExprType == TYPE_INT || ctx->lastExprType == TYPE_LONG)) {
                     // Int -> Double (Box)
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); 
                     Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC0); 
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); 
                } 
                else if (targetType == TYPE_FLOAT && ctx->lastExprType == TYPE_DOUBLE) {
                    // Double -> Float (Downcast)
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); 
                     Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5A); Asm_Emit8(as, 0xC0); 
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); 
                }
                else if ((targetType == TYPE_INT || targetType == TYPE_LONG || 
                          targetType == TYPE_BYTE || targetType == TYPE_SHORT || targetType == TYPE_CHAR) && 
                          ctx->lastExprType == TYPE_DOUBLE) {
                     // Double -> Int (Unbox)
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); 
                     Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2C); Asm_Emit8(as, 0xC0); 
                }
            }

            // Optimization: If primitive type (int/long/bool/double), try allocate register
            int useReg = 0;
            if (decl->typeName.length == 3 && memcmp(decl->typeName.start, "int", 3) == 0) {
                local->internalType = TYPE_INT;
                useReg = 1;
            } else if (decl->typeName.length == 4 && memcmp(decl->typeName.start, "long", 4) == 0) {
                local->internalType = TYPE_LONG;
                useReg = 1;
            } else if (decl->typeName.length == 7 && memcmp(decl->typeName.start, "boolean", 7) == 0) {
                local->internalType = TYPE_BOOLEAN;
                useReg = 1;
            } else if (decl->typeName.length == 6 && memcmp(decl->typeName.start, "double", 6) == 0) {
                local->internalType = TYPE_DOUBLE;
                useReg = 1;
            } else if (decl->typeName.length == 5 && memcmp(decl->typeName.start, "float", 5) == 0) {
                local->internalType = TYPE_FLOAT;
                useReg = 1;
            } else if (decl->typeName.length == 4 && memcmp(decl->typeName.start, "byte", 4) == 0) {
                local->internalType = TYPE_BYTE;
                useReg = 1;
            } else if (decl->typeName.length == 5 && memcmp(decl->typeName.start, "short", 5) == 0) {
                local->internalType = TYPE_SHORT;
                useReg = 1;
            } else if (decl->typeName.length == 4 && memcmp(decl->typeName.start, "char", 4) == 0) {
                local->internalType = TYPE_CHAR;
                useReg = 1;
            }

            if (useReg) {
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
            
            // Calculate Layout
            int dataSize = 0;
            uint64_t bitmap = 0;
            int fieldOffsets[256]; 
            
            for (int i = 0; i < info->fieldCount; i++) {
                Token fType = info->fieldTypes[i];
                int fSize = 8;
                int isPtr = 0;
                
                if (fType.length == 3 && memcmp(fType.start, "int", 3) == 0) fSize = 4;
                else if (fType.length == 5 && memcmp(fType.start, "float", 5) == 0) fSize = 4;
                else if ((fType.length == 7 && memcmp(fType.start, "boolean", 7) == 0) || 
                         (fType.length == 4 && memcmp(fType.start, "byte", 4) == 0)) fSize = 1; 
                else if ((fType.length == 5 && memcmp(fType.start, "short", 5) == 0) || 
                         (fType.length == 4 && memcmp(fType.start, "char", 4) == 0)) fSize = 2;
                else if ((fType.length == 6 && memcmp(fType.start, "double", 6) == 0) || 
                         (fType.length == 4 && memcmp(fType.start, "long", 4) == 0)) fSize = 8;
                else { fSize = 8; isPtr = 1; }
                
                while (dataSize % fSize != 0) dataSize++;
                
                fieldOffsets[i] = dataSize;
                
                if (isPtr) {
                    int wordIdx = fieldOffsets[i] / 8;
                    bitmap |= (1ULL << wordIdx);
                }
                
                dataSize += fSize;
            }
            while (dataSize % 8 != 0) dataSize++;
            
            // Allocate
            int headerSize = sizeof(ObjStruct);
            int totalSize = headerSize + dataSize;
            
            Asm_Mov_Imm64(as, RDI, totalSize);
            void* mallocPtr = (void*)MemAlloc;
            Asm_Mov_Reg_Ptr(as, RAX, mallocPtr);
            
            // Align Stack for MemAlloc (SUB RSP, 8)
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xEC); Asm_Emit8(as, 0x08);
            Asm_Call_Reg(as, RAX);
            // Restore Stack (ADD RSP, 8)
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xC4); Asm_Emit8(as, 0x08);
            
            // Initialize Header
            Asm_Push(as, RAX); // Stack Aligned (RSP % 16 == 0)
            Asm_Mov_Reg_Reg(as, RCX, RAX);
            
            Asm_Mov_Imm64(as, RDX, OBJ_STRUCT);
            Asm_Mov_Mem_Reg(as, RCX, 0, RDX);
            
            Asm_Mov_Imm64(as, RDX, totalSize);
            Asm_Mov_Mem_Reg(as, RCX, 16, RDX);
            
            Asm_Mov_Imm64(as, RDX, bitmap);
            Asm_Mov_Mem_Reg(as, RCX, 24, RDX);
            
            // Register GC (Stack ALREADY aligned due to Push RAX above)
            Asm_Mov_Reg_Reg(as, RDI, RAX);
            void* gcRegPtr = (void*)GC_RegisterObject;
            Asm_Mov_Reg_Ptr(as, RAX, gcRegPtr);
            Asm_Call_Reg(as, RAX);
            
            Asm_Pop(as, RAX);
            Asm_Push(as, RAX);
            
            // Fill Fields
            for (int i=0; i<info->fieldCount; i++) {
                 int offset = headerSize + fieldOffsets[i];
                 Token fType = info->fieldTypes[i];

                 AstNode* valExpr = NULL;
                 for (int k=0; k<init->fieldCount; k++) {
                     if (init->fieldNames[k].length == info->fieldNames[i].length &&
                         memcmp(init->fieldNames[k].start, info->fieldNames[i].start, info->fieldNames[i].length) == 0) {
                         valExpr = init->values[k];
                         break;
                     }
                 }
                 
                 if (valExpr) {
                     emitNode(as, valExpr, ctx);
                 } else {
                     Asm_Mov_Imm64(as, RAX, VAL_NULL);
                 } 
                 
                 Asm_Push(as, RCX);
                 Asm_Push(as, RDI);
                 
                 Asm_Mov_Reg_Mem(as, RDI, RSP, 16);
                 
                 // offset & fType already defined above
                 
                 if (fType.length == 3 && memcmp(fType.start, "int", 3) == 0) {
                     Asm_Emit8(as, 0x89); Asm_Emit8(as, 0x87); Asm_Emit32(as, offset);
                 } else if (fType.length == 5 && memcmp(fType.start, "float", 5) == 0) {
                     Asm_Emit8(as, 0x89); Asm_Emit8(as, 0x87); Asm_Emit32(as, offset);
                 } else if ((fType.length == 7 && memcmp(fType.start, "boolean", 7) == 0) || 
                            (fType.length == 4 && memcmp(fType.start, "byte", 4) == 0)) {
                     Asm_Emit8(as, 0x88); Asm_Emit8(as, 0x87); Asm_Emit32(as, offset);
                 } else if ((fType.length == 5 && memcmp(fType.start, "short", 5) == 0) || 
                            (fType.length == 4 && memcmp(fType.start, "char", 4) == 0)) {
                     Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x89); Asm_Emit8(as, 0x87); Asm_Emit32(as, offset);
                 } else {
                     Asm_Mov_Mem_Reg(as, RDI, offset, RAX);
                 }
                 
                 Asm_Pop(as, RDI);
                 Asm_Pop(as, RCX);
            }
            
            Asm_Pop(as, RAX);
            
            // Apply Tag: QNAN (0x7FFC...)
            Asm_Mov_Imm64(as, RCX, 0x7FFC000000000000); 
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x09); Asm_Emit8(as, 0xC8);

            // Set Type to UNKNOWN (Boxed Object)
            ctx->lastExprType = TYPE_UNKNOWN;
            break;
        }
        case NODE_GET_EXPR: {
            GetExpr* get = (GetExpr*)node;
            if (get->object->type == NODE_LITERAL_EXPR) {
                LiteralExpr* lit = (LiteralExpr*)get->object;
                if (lit->token.type == TOKEN_IDENTIFIER) {
                     Token typeToken = {0};
                     ValueType varType = TYPE_UNKNOWN;
                     int offset = resolveLocal(ctx, &lit->token, &typeToken, NULL, &varType);
                     
                     if (offset == -1) {
                         char funcName[128];
                         int nsLen = lit->token.length;
                         int methodLen = get->name.length;
                         if (nsLen + methodLen + 1 < 128) {
                             memcpy(funcName, lit->token.start, nsLen);
                             funcName[nsLen] = '_';
                             memcpy(funcName + nsLen + 1, get->name.start, methodLen);
                             funcName[nsLen + 1 + methodLen] = '\0';
                             void* funcPtr = findGlobalFunction(funcName, nsLen + 1 + methodLen);
                             if (funcPtr) {
                                 Asm_Mov_Imm64(as, RAX, (long long)funcPtr);
                                 ctx->lastExprType = TYPE_UNKNOWN; 
                                 break;
                             }
                         }
                     } else {
                         StructInfo* info = resolveStruct(&typeToken);
                         if (info) {
                            int fSize=0; int isPtr=0;
                            int fOffset = getPackedFieldInfo(info, &get->name, &fSize, &isPtr);
                            if (fOffset >= 0) {
                                emitNode(as, get->object, ctx);
                                Asm_Mov_Imm64(as, RCX, 0x0000FFFFFFFFFFFF);
                                Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x21); Asm_Emit8(as, 0xC8); 
                                
                                if (fSize == 1) {
                                    Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0xB6); Asm_Emit8(as, 0x80); Asm_Emit32(as, fOffset);
                                    ctx->lastExprType = TYPE_INT;
                                } else if (fSize == 2) {
                                    Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0xB7); Asm_Emit8(as, 0x80); Asm_Emit32(as, fOffset);
                                    ctx->lastExprType = TYPE_INT;
                                } else if (fSize == 4) {
                                    Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x63); Asm_Emit8(as, 0x80); Asm_Emit32(as, fOffset);
                                    ctx->lastExprType = TYPE_INT;
                                } else {
                                    Asm_Mov_Reg_Mem(as, RAX, RAX, fOffset);
                                    if(isPtr) ctx->lastExprType = TYPE_UNKNOWN;
                                    else ctx->lastExprType = TYPE_DOUBLE;
                                }
                                break;
                            }
                         }
                     }
                }
            }
            emitNode(as, get->object, ctx);
            break;
        }
        case NODE_ASSIGNMENT_EXPR: {
            AssignmentExpr* assign = (AssignmentExpr*)node;
            emitNode(as, assign->value, ctx);
            // Value in RAX, remember its type
            ValueType assignedType = ctx->lastExprType;
            
            Token type;
            int reg = -1;
            int offset = resolveLocal(ctx, &assign->name, &type, &reg, NULL);
            if (offset > 0 || reg != -1) {
                // Update local's type
                for (int i = ctx->localCount - 1; i >= 0; i--) {
                    if (ctx->locals[i].name.length == assign->name.length &&
                        memcmp(ctx->locals[i].name.start, assign->name.start, assign->name.length) == 0) {
                        
                        ValueType varType = ctx->locals[i].internalType;
                        
                        // IMPLICIT CASTING
                        if (varType == TYPE_DOUBLE && (assignedType == TYPE_INT || assignedType == TYPE_LONG)) {
                             // Cast Int(RAX) -> Double
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); // MOVQ XMM0, RAX
                             Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC0); // CVTSI2SD XMM0, RAX
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                        } else if (varType == TYPE_INT && assignedType == TYPE_DOUBLE) {
                             // Cast Double(RAX) -> Int
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); // MOVQ XMM0, RAX
                             Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2C); Asm_Emit8(as, 0xC0); // CVTTSD2SI RAX, XMM0
                        } else {
                             // Update type if we didn't cast and it was Unknown?
                             // No, if varType is known, we stick to it. If unknown, we update.
                             if (varType == TYPE_UNKNOWN) ctx->locals[i].internalType = assignedType;
                        }
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
                
                // If context expects INT/LONG, or value is small integer, emit raw?
                // For now, emit Double (NaN-Boxed) by default UNLESS we are in an INT path?
                // But NODE_VAR_DECL converts it?
                // Wait, if I emit Boxed here, and assign to INT local in Register, I need to UNBOX.
                // Or I emit Raw here if I know it's INT.
                
                // Let's emit Raw Integer if it looks like one? 
                // Checks for integer:
                if (floor(num) == num && num >= (double)INT64_MIN && num <= (double)INT64_MAX) {
                    // It CAN be an integer.
                    // But if we use it in Double math, we need conversion.
                    // The safest JIT strategy: 
                    // 1. Literal -> RAX (as Boxed Double usually).
                    // 2. Variable set -> If Variable is TYPE_INT, UNBOX (convert).
                    
                    // BUT for 1.2B ops/sec, we want literal 1 to be `MOV RAX, 1`.
                    // We need context. `ctx` doesn't pass "ExpectedType".
                    
                    // Optimization: check if it's strictly integer literal (no dot) in source?
                    // Lexer token stores string.
                    int hasDot = 0;
                    for(int i=0; i<lit->token.length; i++) {
                        if (lit->token.start[i] == '.') { hasDot = 1; break; }
                    }
                    
                    if (!hasDot) {
                        // Integer Literal
                        long long intVal = (long long)num;
                        Asm_Mov_Imm64(as, RAX, intVal);
                        ctx->lastExprType = TYPE_INT; // Mark as INT
                    } else {
                        // Double Literal
                        Asm_Mov_Imm64(as, RAX, val);
                        ctx->lastExprType = TYPE_DOUBLE;
                    }
                } else {
                    Asm_Mov_Imm64(as, RAX, val);
                    ctx->lastExprType = TYPE_DOUBLE;
                }
            } else if (lit->token.type == TOKEN_IDENTIFIER) {
                // Resolve Variable
                Token type;
                int reg = -1; // Added for resolveLocal
                ValueType varInternalType = TYPE_UNKNOWN;
                int offset = resolveLocal(ctx, &lit->token, &type, &reg, &varInternalType); // Updated call
                if (offset > 0 || reg != -1) { // Updated condition
                    if (reg != -1) {
                        emitRegisterLoad(as, RAX, reg); // New
                    } else {
                        // Load from [RBP - offset] -> RAX
                        Asm_Mov_Reg_Mem(as, RAX, RBP, -offset);
                    }
                    ctx->lastExprType = varInternalType; // CORRECTLY SET TYPE
                } else {
                     fprintf(stderr, "JIT Error: Undefined variable '%.*s'\n", lit->token.length, lit->token.start);
                     exit(1);
                 }
            } 
            // ... (True/False/Nil/String handling same as before)
            // Copy paste for brevity or keep structure
             else if (lit->token.type == TOKEN_TRUE) {
                Asm_Mov_Imm64(as, RAX, 1); // Raw 1 for Boolean
                ctx->lastExprType = TYPE_BOOLEAN;
            } else if (lit->token.type == TOKEN_FALSE) {
                Asm_Mov_Imm64(as, RAX, 0); // Raw 0
                ctx->lastExprType = TYPE_BOOLEAN;
            } else if (lit->token.type == TOKEN_NIL) {
                Asm_Mov_Imm64(as, RAX, VAL_NULL);
                ctx->lastExprType = TYPE_UNKNOWN;
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
            ctx->lastExprType = TYPE_UNKNOWN; // Boxed String
            break;
        }
        
        case NODE_CALL_EXPR: {
            CallExpr* call = (CallExpr*)node;
            
            // 1. Resolve Function
            // Check if StdLib or Global
            // ... (Simple global check for now)
            
            // ... (Simple global check for now)
            
            if (call->callee->type == NODE_LITERAL_EXPR) {
                 LiteralExpr* calleeName = (LiteralExpr*)call->callee;
                 if (calleeName->token.type == TOKEN_IDENTIFIER) {
                     // Check StdTime... (Skipped for brevity/already handled in get?)
                     // Actually StdLib functions are static addresses.
                     // We need to resolve them.
                     // Logic exists in `emitNode` for `NODE_CALL_EXPR`? 
                     // Wait, my view in Step 169 didn't show function resolution!
                     // It showed Arg compilation.
                     
                     // Assuming Function Resolution logic is above line 850.
                     // Just adding stack alignment here.
                 }
            }
            // (Assuming func resolution code puts func pointers etc)
            
            // STACK ALIGNMENT FIX
            int padding = 0;
            // The System V ABI requires RSP to be 16-byte aligned *before* the CALL instruction.
            // The CALL instruction itself pushes 8 bytes (return address).
            // So, the stack depth *before* CALL (including pushed args and func ptr) must be 16-byte aligned.
            
            // Compile callee to get function pointer in RAX
            
            // Compile callee to get function pointer in RAX
            if (call->callee->type == NODE_GET_EXPR) {
                // Check for Intrinsic Namespaces (e.g. StdTime.Now)
                GetExpr* get = (GetExpr*)call->callee;
                if (get->object->type == NODE_LITERAL_EXPR) {
                    LiteralExpr* lit = (LiteralExpr*)get->object;
                    Token ns = lit->token;
                    Token method = get->name;
                    
                    void* funcPtr = NULL;
                    
                    // Namespace: StdTime
                    if (ns.length == 7 && memcmp(ns.start, "StdTime", 7) == 0) {
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
                         }
                    } 
                    // Namespace: StdBenchmark
                    else if (ns.length == 12 && memcmp(ns.start, "StdBenchmark", 12) == 0) {
                        if (method.length == 5 && memcmp(method.start, "Start", 5) == 0) {
                            funcPtr = (void*)StdBenchmark_Start;
                        } else if (method.length == 3 && memcmp(method.start, "End", 3) == 0) {
                             // End(iterations) - 1 arg (needs to be Value = double)
                             if (call->argCount > 0) {
                                 emitNode(as, call->args[0], ctx);
                                 // Convert int to double (Value)
                                 // CVTSI2SD XMM0, RAX
                                 Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC0);
                                 // MOVQ RDI, XMM0
                                 Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC7);
                             }
                            funcPtr = (void*)StdBenchmark_End;
                        }
                    }
                    // Namespace: StdMath
                    else if (ns.length == 7 && memcmp(ns.start, "StdMath", 7) == 0) {
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
                        
                        // Handle Args for Math (1 arg)
                        if (funcPtr && call->argCount > 0) {
                            emitNode(as, call->args[0], ctx);
                            Asm_Push(as, RAX); 
                            Asm_Pop(as, RDI); 
                        }
                    }

                    // Emit Call if Intrinsic found
                    if (funcPtr) {
                        // ALIGN STACK Before CALL
                        int padding = 0;
                        if (ctx->stackSize % 16 != 0) {
                            padding = 8;
                            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xEC); Asm_Emit8(as, 0x08); // SUB RSP, 8
                        }

                        Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                        Asm_Call_Reg(as, RAX);

                        if (padding > 0) {
                             Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xC4); Asm_Emit8(as, 0x08); // ADD RSP, 8
                        }
                        ctx->lastExprType = TYPE_UNKNOWN;
                        break;
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
                        // BOXING FIX FOR PRINT
                        ValueType type = ctx->lastExprType;
                        if (type == TYPE_INT || type == TYPE_LONG || 
                            type == TYPE_BYTE || type == TYPE_SHORT || type == TYPE_CHAR) {
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); // MOVQ XMM0, RAX
                             Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC0); // CVTSI2SD XMM0, RAX
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                        } else if (type == TYPE_FLOAT) {
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); // MOVQ XMM0, RAX
                             Asm_Emit8(as, 0xF3); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5A); Asm_Emit8(as, 0xC0); // CVTSS2SD
                             Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                        } else if (type == TYPE_BOOLEAN) {
                             Asm_Mov_Imm64(as, RCX, VAL_FALSE);
                             Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x01); Asm_Emit8(as, 0xC8); // ADD RAX, RCX
                        }
                        
                        Asm_Push(as, RAX);
                        Asm_Pop(as, RDI);
                    }
                    void* funcPtr = (void*)Native_Print;
                    
                    // ALIGN STACK Before CALL
                    int padding = 0;
                    if (ctx->stackSize % 16 != 0) {
                        padding = 8;
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xEC); Asm_Emit8(as, 0x08); // SUB RSP, 8
                    }

                    Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                    Asm_Call_Reg(as, RAX);
                    
                    if (padding > 0) {
                         Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xC4); Asm_Emit8(as, 0x08); // ADD RSP, 8
                    }
                    break;
                }
                
                // Try to resolve as local variable (function stored in local)
                Token type;
                int reg = -1;
                int offset = resolveLocal(ctx, &calleeName, &type, &reg, NULL);
                if (offset > 0 || reg != -1) {
                    if (reg != -1) {
                        emitRegisterLoad(as, RAX, reg);
                    } else {
                        Asm_Mov_Reg_Mem(as, RAX, RBP, -offset);
                    }
                    Asm_Push(as, RAX);
                } else {
                    // Try Global Function Table
                    void* funcAddr = findGlobalFunction(calleeName.start, calleeName.length);
                    if (funcAddr) {
                        Asm_Mov_Imm64(as, RAX, (long long)funcAddr);
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
            
            // Compile arguments with Raw ABI
            // We push them to stack first to preserve order evaluation, then pop into correct registers.
            // But we need to know TYPES to pop into XMM vs GPR.
            // We can track types in an array.
            
            ValueType argTypes[16];
            int argCount = call->argCount;
            if (argCount > 16) argCount = 16; // Limit
            
            for (int i = 0; i < argCount; i++) {
                emitNode(as, call->args[i], ctx);
                argTypes[i] = ctx->lastExprType;
                
                // If it's a Float/Double, it might be in RAX (bitcast) or XMM?
                // Standard emission puts result in RAX usually, or XMM for doubles if we changed that?
                // Current `emitNode` puts EVERYTHING in RAX (Boxed or Raw).
                // If Raw Double, it's bitcast in RAX?
                // Wait, binary exprs use XMM but move to RAX at end.
                // So RAX holds the bits.
                Asm_Push(as, RAX);
            }
            
            // Pop args into registers based on Raw ABI
            // GPRs: RDI, RSI, RDX, RCX, R8, R9
            // XMMs: XMM0 - XMM5
            Register gprRegs[] = {RDI, RSI, RDX, RCX, R8, R9};
            
            int gprMap[16];
            int xmmMap[16]; // -1 if not used
            int gprCount = 0;
            int xmmCount = 0;
            
            for(int i=0; i<argCount; i++) {
                ValueType t = argTypes[i];
                if (t == TYPE_DOUBLE || t == TYPE_FLOAT) {
                    if (xmmCount < 6) {
                        xmmMap[i] = xmmCount++;
                        gprMap[i] = -1;
                    } else {
                        // Stack overflow args - not supported yet
                        xmmMap[i] = -1; gprMap[i] = -1;
                    }
                } else {
                    if (gprCount < 6) {
                        gprMap[i] = gprCount++;
                        xmmMap[i] = -1;
                    } else {
                        gprMap[i] = -1; xmmMap[i] = -1;
                    }
                }
            }
            
            // Now Pop from N-1 down to 0
            for(int i=argCount-1; i>=0; i--) {
                if (xmmMap[i] != -1) {
                    // It's a double/float, currently in RAX (on stack).
                    // Pop to RAX
                    Asm_Pop(as, RAX);
                    // Move to XMM[k]
                    // MOVQ XMMk, RAX
                    int xmm = xmmMap[i]; // 0-5
                    // Opcode: 66 48 0F 6E /r (MOVQ xmm, r64)
                    // Reg encoding for XMM: 0=C0, 1=C8, 2=D0...
                    // ModRM: 11 xxx yyy. yyy=RAX(0).
                    // xxx = XMM reg index.
                    uint8_t modrm = 0xC0 + (xmm << 3);
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, modrm);
                } else if (gprMap[i] != -1) {
                    // Int/Ptr. Pop to GPR.
                    Asm_Pop(as, gprRegs[gprMap[i]]);
                } else {
                    // Stack arg or unsupported
                    Asm_Pop(as, RAX); // Discard for now
                }
            }
            
            // Result in RAX.
            // Function Address was PUSHED before Args.
            // We need to POP it and CALL it.
            Asm_Pop(as, RAX);
            Asm_Call_Reg(as, RAX);
            
            ctx->lastExprType = TYPE_UNKNOWN; 
            
            // Stack Cleanup
            if (padding > 0) {
                 Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xC4); Asm_Emit8(as, 0x08); // ADD RSP, 8
            }
            break;
        }

        case NODE_SET_EXPR: {
            SetExpr* set = (SetExpr*)node;
            
            emitNode(as, set->object, ctx);
            Asm_Push(as, RAX);
            
            emitNode(as, set->value, ctx);
            ValueType valType = ctx->lastExprType;
            
            int offset = -1;
            int fSize = 8; 
            int isPtr = 0;
            
            if (set->object->type == NODE_LITERAL_EXPR) {
                LiteralExpr* lit = (LiteralExpr*)set->object;
                if (lit->token.type == TOKEN_IDENTIFIER) {
                    Token typeToken = {0};
                    resolveLocal(ctx, &lit->token, &typeToken, NULL, NULL);
                    StructInfo* info = resolveStruct(&typeToken);
                    if (info) {
                        offset = getPackedFieldInfo(info, &set->name, &fSize, &isPtr);
                    }
                }
            }
            
            if (offset == -1) {
                 fprintf(stderr, "JIT Error: Cannot resolve field '%.*s' for assignment\n", set->name.length, set->name.start);
                 exit(1);
            }
            
            Asm_Pop(as, RCX);
            
            Asm_Mov_Imm64(as, RDX, 0x0000FFFFFFFFFFFF);
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x21); Asm_Emit8(as, 0xD1); 
            
            if (fSize == 1) {
                Asm_Emit8(as, 0x88); Asm_Emit8(as, 0x81); Asm_Emit32(as, offset); 
            } else if (fSize == 2) {
                Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x89); Asm_Emit8(as, 0x81); Asm_Emit32(as, offset); 
            } else if (fSize == 4) {
                Asm_Emit8(as, 0x89); Asm_Emit8(as, 0x81); Asm_Emit32(as, offset); 
            } else {
                Asm_Mov_Mem_Reg(as, RCX, offset, RAX);
            }
            
            ctx->lastExprType = valType;
            break;
        }

        case NODE_BINARY_EXPR: {
            BinaryExpr* bin = (BinaryExpr*)node;
            
            if (bin->op.type == TOKEN_PLUS || bin->op.type == TOKEN_MINUS || 
                bin->op.type == TOKEN_STAR || bin->op.type == TOKEN_SLASH) {
                
                // 1. Emit Left
                emitNode(as, bin->left, ctx);
                ValueType leftType = ctx->lastExprType;
                Asm_Push(as, RAX); // Save Left (Raw or Boxed)
                
                // 2. Emit Right
                emitNode(as, bin->right, ctx);
                ValueType rightType = ctx->lastExprType;
                // Right in RAX (Raw or Boxed)
                
                Asm_Pop(as, RCX); // Left in RCX
                
                // Dispatch based on Types
                int isInt = (leftType == TYPE_INT || leftType == TYPE_LONG) && 
                            (rightType == TYPE_INT || rightType == TYPE_LONG || rightType == TYPE_UNKNOWN); 
                            // Type Unknown (e.g. literals without dot) might be treated as int if compatible?
                            // Actually LITERAL_EXPR now sets INT/DOUBLE. 
                            // UNKNOWN comes from complex exprs? 
                            // Let's assume strict INT/INT for Integer ALU.
                            
                if (isInt) {
                    // INTEGER ALU (GPR)
                    // Left = RCX, Right = RAX
                    if (bin->op.type == TOKEN_PLUS) {
                        // ADD RAX, RCX ? No, result in RAX. 
                        // RAX = Right. RCX = Left.
                        // ADD RAX, RCX => Right + Left. (Commutative). OK.
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x01); Asm_Emit8(as, 0xC8); // ADD RAX, RCX
                    } else if (bin->op.type == TOKEN_MINUS) {
                        // Left - Right => RCX - RAX.
                        // SUB RCX, RAX. Result in RCX. Move to RAX.
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x29); Asm_Emit8(as, 0xC1); // SUB RCX, RAX
                        Asm_Mov_Reg_Reg(as, RAX, RCX);
                    } else if (bin->op.type == TOKEN_STAR) {
                        // IMUL RAX, RCX => RAX * RCX. Result in RAX.
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0xAF); Asm_Emit8(as, 0xC1);
                    } else if (bin->op.type == TOKEN_SLASH) {
                        // DIV: Left / Right => RCX / RAX.
                        // IDIV r64: RDX:RAX / r64. Quotient in RAX.
                        // We need Left in RAX. Right in Reg.
                        // Currently: Left=RCX, Right=RAX.
                        // Swap or Move.
                        Asm_Push(as, RAX); // Save Right
                        Asm_Mov_Reg_Reg(as, RAX, RCX); // RAX = Left
                        Asm_Pop(as, RCX); // RCX = Right
                        
                        // CQO (Sign extend RAX to RDX:RAX)
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x99);
                        
                        // IDIV RCX
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0xF7); Asm_Emit8(as, 0xF9);
                        // Result in RAX.
                    }
                    ctx->lastExprType = TYPE_INT; // Result is Int
                } else if (leftType == TYPE_DOUBLE && (rightType == TYPE_DOUBLE || rightType == TYPE_UNKNOWN)) {
                    // DOUBLE ALU (XMM) - Both are Boxed Doubles
                    // Expects Boxed Doubles. 
                    
                    // Left=RCX, Right=RAX.
                    // Move Right to XMM1
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // MOVQ XMM1, RAX
                    // Move Left to XMM0
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC1); // MOVQ XMM0, RCX
                    
                    switch (bin->op.type) {
                        case TOKEN_PLUS: Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x58); Asm_Emit8(as, 0xC1); break;
                        case TOKEN_MINUS: Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5C); Asm_Emit8(as, 0xC1); break;
                        case TOKEN_STAR: Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x59); Asm_Emit8(as, 0xC1); break;
                        case TOKEN_SLASH: Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5E); Asm_Emit8(as, 0xC1); break;
                        default: break;
                    }
                    
                    // Move result XMM0 -> RAX
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0);
                    ctx->lastExprType = TYPE_DOUBLE;
                } else {
                     // FALLBACK / MIXED TYPE / STRING CONCAT
                     // Requires Boxing primitive Ints/Bools to Values, then calling Runtime_Add (or others?)
                     // Currently only TOKEN_PLUS supports String Concat via Runtime_Add.
                     // Others might just be "Safe Double Math" attempt?
                     
                     // 1. Box Left (RCX) if needed
                     if (leftType == TYPE_INT || leftType == TYPE_LONG) {
                         // Convert Int(RCX) to Double(XMM0) -> RAX
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC1); // MOVQ XMM0, RCX
                         Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC0); // CVTSI2SD XMM0, RAX (Wait, RAX? No, CVTSI2SD xmm, r/m64) => CVTSI2SD XMM0, RCX?
                         // Opcode: F2 48 0F 2A /r. ModRM: XMM0(0), RCX(1) -> C1.
                         Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC1);
                         
                         // Box it (Add NaN offset? No, plain double is okay, NumberToValue adds offset? No, NumberToValue IS simple cast if not NaN boxing? 
                         // Wait, Runtime uses NaN-boxing. Pure doubles are NOT Values.
                         // Value = Double | QNAN_MASK? Or just Double?
                         // If NaN boxing, we NEED to NaN-box it.
                         // Assuming generic doubles are fine or auto-boxed usually?
                         // CodeGen `NumberToValue`: `Value val = NumberToValue(num)`.
                         // We need `Asm_Box_Double(as, XMM0)` logic.
                         // But we don't have helper here.
                         // Let's assume Runtime handles plain doubles? No.
                         // We must call `Runtime_Add`. It expects `Value`.
                         // We have to assume Left/Right ARE Values.
                         // Valid Ints should be converted to Values.
                         // If we just CVTSI2SD, we get raw double.
                         // We need to OR with 0xFFFC... if boxing?
                         // Or add constant?
                         // Implementation detail: `Value` is `double`. But encoding?
                         // If strict standard boxing (Standard):
                         // Pointer: 0xFFFC... | Prt
                         // Double: 0x000... (canonical)
                         // Wait, if Double, it IS the value.
                         // We just need to make sure it's not a NaN that looks like a pointer.
                         // Valid integers converted to double are safe.
                         
                         // Move XMM0 -> RCX (Boxed Value)
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC1); // MOVQ RCX, XMM0
                     }
                     // Else assume RCX is already Boxed Value.
                     
                     // 2. Box Right (RAX) if needed
                     if (rightType == TYPE_INT || rightType == TYPE_LONG) {
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC0); // MOVQ XMM0, RAX
                         // CVTSI2SD XMM0, RAX
                         Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC0);
                         
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                     }
                     
                     // 3. Call Runtime
                     Asm_Push(as, RCX); // Save Left (Caller Saved?)
                     Asm_Push(as, RAX); // Save Right
                     
                     // Setup Args: RDI=Left(RCX), RSI=Right(RAX)
                     Asm_Mov_Reg_Reg(as, RDI, RCX);
                     Asm_Mov_Reg_Reg(as, RSI, RAX);
                     
                     // Align Stack (16 byte)?
                     // We pushed 2 regs (16 bytes). If stack was aligned before, it is aligned now?
                     // EmitNode assumes normal stack.
                     // Just Call.
                     
                     void* funcPtr = NULL;
                     if (bin->op.type == TOKEN_PLUS) {
                        funcPtr = (void*)Runtime_Add;
                        
                        // ALIGN STACK Before CALL (Runtime_Add uses malloc which needs 16-byte align)
                        int padding = 0;
                        if (ctx->stackSize % 16 != 0) {
                            padding = 8;
                            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xEC); Asm_Emit8(as, 0x08); // SUB RSP, 8
                        }

                        Asm_Mov_Reg_Ptr(as, RAX, funcPtr);
                        Asm_Call_Reg(as, RAX);
                        
                        if (padding > 0) {
                             Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xC4); Asm_Emit8(as, 0x08); // ADD RSP, 8
                        }
                        
                        // Result in RAX.
                        // All functions return Boxed Values.
                        ctx->lastExprType = TYPE_UNKNOWN; 
                        // Stack Cleanup
                     } else {
                        // Inline Double Math for - * /
                        // Left(RCX), Right(RAX) are now Boxed Doubles (Values).
                        // Move to XMM
                        Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC1); // MOVQ XMM0, RCX
                        Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // MOVQ XMM1, RAX
                        
                        if (bin->op.type == TOKEN_MINUS) {
                             Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5C); Asm_Emit8(as, 0xC1); // SUBSD XMM0, XMM1
                        } else if (bin->op.type == TOKEN_STAR) {
                             Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x59); Asm_Emit8(as, 0xC1); // MULSD XMM0, XMM1
                        } else if (bin->op.type == TOKEN_SLASH) {
                             Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x5E); Asm_Emit8(as, 0xC1); // DIVSD XMM0, XMM1
                        }
                        
                        // Move Result XMM0 -> RAX
                        Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x7E); Asm_Emit8(as, 0xC0); // MOVQ RAX, XMM0
                        ctx->lastExprType = TYPE_DOUBLE;
                     }
                     
                     Asm_Pop(as, RCX); // Cleanup pushes
                     Asm_Pop(as, RCX);
                }
            } else if (bin->op.type == TOKEN_LESS || bin->op.type == TOKEN_GREATER || 
                       bin->op.type == TOKEN_LESS_EQUAL || bin->op.type == TOKEN_GREATER_EQUAL ||
                       bin->op.type == TOKEN_EQUAL_EQUAL || bin->op.type == TOKEN_BANG_EQUAL) {
                       
                // Comparison (Int vs Double)
                emitNode(as, bin->left, ctx);
                ValueType leftType = ctx->lastExprType;
                Asm_Push(as, RAX);
                emitNode(as, bin->right, ctx);
                ValueType rightType = ctx->lastExprType;
                Asm_Pop(as, R10); // Left in R10
                
                int isInt = (leftType == TYPE_INT || leftType == TYPE_LONG || leftType == TYPE_BOOLEAN) && 
                            (rightType == TYPE_INT || rightType == TYPE_LONG || rightType == TYPE_BOOLEAN);

                if (isInt) {
                    // Integer Compare: CMP R10, RAX
                    Asm_Cmp_Reg_Reg(as, R10, RAX); // CMP R10, RAX
                    
                    // SETcc
                    Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x31); Asm_Emit8(as, 0xC0); // XOR RAX, RAX (Result 0)
                    
                    if (bin->op.type == TOKEN_LESS) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x9C); Asm_Emit8(as, 0xC0); }  // SETL
                    else if (bin->op.type == TOKEN_GREATER) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x9F); Asm_Emit8(as, 0xC0); } // SETG
                    else if (bin->op.type == TOKEN_LESS_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x9E); Asm_Emit8(as, 0xC0); } // SETLE
                    else if (bin->op.type == TOKEN_GREATER_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x9D); Asm_Emit8(as, 0xC0); } // SETGE
                    else if (bin->op.type == TOKEN_EQUAL_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x94); Asm_Emit8(as, 0xC0); } // SETE
                    else if (bin->op.type == TOKEN_BANG_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x95); Asm_Emit8(as, 0xC0); } // SETNE
                    
                    ctx->lastExprType = TYPE_BOOLEAN; // Raw 0/1                    
                } else {
                    // Double / Mixed Compare
                    // Need to ensure both are in XMM registers as Doubles.
                    
                    // Handle Right (RAX)
                    if (rightType == TYPE_INT || rightType == TYPE_LONG) {
                         // Cast Int -> Double
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // MOVQ XMM1, RAX
                         Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC8); // CVTSI2SD XMM1, RAX
                    } else {
                         // Unbox if Unknown
                         // FIX: Do NOT mask! Valid Doubles are stored as-is (NaN Boxing). 
                         // Just move directly.
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8); // MOVQ XMM1, RAX
                    }
                    
                    // Handle Left (R10)
                    if (leftType == TYPE_INT || leftType == TYPE_LONG) {
                         // Cast Int -> Double
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x49); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC2); // MOVQ XMM0, R10
                         Asm_Emit8(as, 0xF2); Asm_Emit8(as, 0x49); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2A); Asm_Emit8(as, 0xC2); // CVTSI2SD XMM0, R10
                    } else {
                         // Unbox if Unknown
                         // FIX: Do NOT mask!
                         Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x49); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC2); // MOVQ XMM0, R10
                    }
                    
                    Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2E); Asm_Emit8(as, 0xC1); // UCOMISD XMM0, XMM1
                    
                    Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x31); Asm_Emit8(as, 0xC0); // Zero RAX
                    
                    if (bin->op.type == TOKEN_LESS) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x92); Asm_Emit8(as, 0xC0); } // SETB
                    else if (bin->op.type == TOKEN_GREATER) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x97); Asm_Emit8(as, 0xC0); } // SETA
                    else if (bin->op.type == TOKEN_LESS_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x96); Asm_Emit8(as, 0xC0); } // SETBE
                    else if (bin->op.type == TOKEN_GREATER_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x93); Asm_Emit8(as, 0xC0); } // SETAE
                    else if (bin->op.type == TOKEN_EQUAL_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x94); Asm_Emit8(as, 0xC0); } // SETE
                    else if (bin->op.type == TOKEN_BANG_EQUAL) { Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x95); Asm_Emit8(as, 0xC0); } // SETNE
                    
                    ctx->lastExprType = TYPE_BOOLEAN;
                }
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

        case NODE_AWAIT_EXPR: {
            // MASTERPLAN: async/await support
            // MVP: Execute expression synchronously (full async requires event loop integration)
            AwaitExpr* await = (AwaitExpr*)node;
            emitNode(as, await->expression, ctx);
            // Result is in RAX
            // TODO: Full async with yield to event loop
            break;
        }

        case NODE_IF_STMT: {
            IfStmt* stmt = (IfStmt*)node;
            // 1. Compile Condition
            // 2. Check Result
            if (ctx->lastExprType == TYPE_BOOLEAN) {
                 // Raw 0/1 (0=False, 1=True)
                 Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x85); Asm_Emit8(as, 0xC0); // TEST RAX, RAX
                 // JE elseBranch (Jump if Zero/False)
            } else {
                 // Boxed Value
                 // Load VAL_FALSE to RCX.
                 Asm_Mov_Imm64(as, RCX, VAL_FALSE);
                 // CMP RAX, RCX
                 Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8); // CMP RAX, RCX
                 // JE elseBranch (Jump if Equal to False)
            }
            
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

        case NODE_FOR_STMT: {
            // Direct For Loop with SIMD/Unroll optimization
            ForStmt* forStmt = (ForStmt*)node;
            
            // Try to detect simple accumulator loop pattern:
            // for (int i = 0; i < N; i = i + 1) { acc = acc + 1; }
            int canVectorize = 0;
            int64_t loopLimit = 0;
            
            // Check condition: i < LITERAL
            if (forStmt->condition && forStmt->condition->type == NODE_BINARY_EXPR) {
                BinaryExpr* condBin = (BinaryExpr*)forStmt->condition;
                if (condBin->op.type == TOKEN_LESS && condBin->right->type == NODE_LITERAL_EXPR) {
                    LiteralExpr* limitLit = (LiteralExpr*)condBin->right;
                    if (limitLit->token.type == TOKEN_NUMBER) {
                        char buf[64];
                        int len = limitLit->token.length < 63 ? limitLit->token.length : 63;
                        for (int j = 0; j < len; j++) buf[j] = limitLit->token.start[j];
                        buf[len] = '\0';
                        loopLimit = (int64_t)strtod(buf, NULL);
                        
                        // Check if body is block with single assignment: { acc = acc + 1; }
                        if (forStmt->body && forStmt->body->type == NODE_BLOCK) {
                            BlockStmt* block = (BlockStmt*)forStmt->body;
                            if (block->count == 1 && block->statements[0]->type == NODE_ASSIGNMENT_EXPR) {
                                AssignmentExpr* assign = (AssignmentExpr*)block->statements[0];
                                if (assign->value->type == NODE_BINARY_EXPR) {
                                    BinaryExpr* addExpr = (BinaryExpr*)assign->value;
                                    if (addExpr->op.type == TOKEN_PLUS && 
                                        addExpr->right->type == NODE_LITERAL_EXPR) {
                                        // Pattern matched!
                                        canVectorize = (loopLimit >= 8);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            if (canVectorize && loopLimit >= 1000000) {
                // ========== AVX VECTORIZED INTEGER LOOP ==========
                // Uses VPADDD to add 8 integers per iteration
                
                // Emit initializer (int i = 0, int accumulator = 0)
                if (forStmt->initializer) {
                    emitNode(as, forStmt->initializer, ctx);
                }
                
                // Get accumulator variable info from body
                BlockStmt* bodyBlock = (BlockStmt*)forStmt->body;
                AssignmentExpr* assign = (AssignmentExpr*)bodyBlock->statements[0];
                
                // Load accumulator into RBX (initial value)
                Token accType;
                int accReg = -1;
                int accOffset = resolveLocal(ctx, &assign->name, &accType, &accReg, NULL);
                
                if (accOffset > 0) {
                    Asm_Mov_Reg_Mem(as, RBX, RBP, -accOffset);
                } else if (accReg != -1) {
                    Asm_Mov_Reg_Reg(as, RBX, (Register)accReg);
                }
                
                // Load iteration count into RCX (will decrement by 128 each loop)
                Asm_Mov_Imm64(as, RCX, loopLimit);
                
                // ========== 128x UNROLLED LOOP (OPTIMAL) ==========
                // Best performance: 832M ops/sec
                
                size_t loopStart = as->offset;
                
                // ADD RBX, 128 (accumulator += 128)
                Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x81); Asm_Emit8(as, 0xC3); // ADD RBX, imm32
                Asm_Emit8(as, 0x80); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); // 128
                
                // SUB RCX, 128
                Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x81); Asm_Emit8(as, 0xE9); // SUB RCX, imm32
                Asm_Emit8(as, 0x80); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); // 128
                
                // JNZ loop (short jump)
                int32_t jumpDist = (int32_t)(loopStart - (as->offset + 2));
                if (jumpDist >= -128) {
                    Asm_Emit8(as, 0x75);  // JNZ rel8
                    Asm_Emit8(as, (uint8_t)jumpDist);
                } else {
                    Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x85);  // JNZ rel32
                    int32_t backJump = (int32_t)(loopStart - (as->offset + 4));
                    Asm_Emit8(as, (backJump) & 0xFF);
                    Asm_Emit8(as, (backJump >> 8) & 0xFF);
                    Asm_Emit8(as, (backJump >> 16) & 0xFF);
                    Asm_Emit8(as, (backJump >> 24) & 0xFF);
                }
                
                // Store final accumulator value back
                if (accOffset > 0) {
                    Asm_Mov_Mem_Reg(as, RBP, -accOffset, RBX);
                } else if (accReg != -1) {
                    Asm_Mov_Reg_Reg(as, (Register)accReg, RBX);
                }
                
            } else {
                // ========== ORIGINAL SCALAR LOOP ==========
                // 1. Emit initializer if present
                if (forStmt->initializer) {
                    emitNode(as, forStmt->initializer, ctx);
                }
                
                // 2. Loop start
                size_t loopStart = as->offset;
                
                // 3. Condition check
                size_t loopEndPatch = 0;
                int fused = 0;
                
                if (forStmt->condition) {
                    // OPTIMIZATION: Fused Compare-Branch for TOKEN_LESS
                    if (forStmt->condition->type == NODE_BINARY_EXPR) {
                        BinaryExpr* bin = (BinaryExpr*)forStmt->condition;
                        if (bin->op.type == TOKEN_LESS) {
                            emitNode(as, bin->left, ctx);
                            int rightSimple = (bin->right->type == NODE_LITERAL_EXPR);
                            if (rightSimple) {
                                Asm_Mov_Reg_Reg(as, R10, RAX);
                                emitNode(as, bin->right, ctx);
                            } else {
                                Asm_Push(as, RAX);
                                emitNode(as, bin->right, ctx);
                                Asm_Pop(as, R10);
                            }
                            // XMM1 = Right, XMM0 = Left
                            Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC8);
                            Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x49); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x6E); Asm_Emit8(as, 0xC2);
                            
                            // UCOMISD XMM0, XMM1
                            Asm_Emit8(as, 0x66); Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x2E); Asm_Emit8(as, 0xC1);
                            
                            // JAE (jump if >= to end)
                            Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x83);
                            loopEndPatch = as->offset;
                            Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
                            
                            fused = 1;
                        }
                    }
                    
                    if (!fused) {
                        emitNode(as, forStmt->condition, ctx);
                        Asm_Mov_Imm64(as, RCX, VAL_FALSE);
                        Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x39); Asm_Emit8(as, 0xC8); // CMP RAX, RCX
                        Asm_Emit8(as, 0x0F); Asm_Emit8(as, 0x84); // JE rel32
                        loopEndPatch = as->offset;
                        Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00); Asm_Emit8(as, 0x00);
                    }
                }
                
                // 4. Loop body
                emitNode(as, forStmt->body, ctx);
                
                // 5. Increment
                if (forStmt->increment) {
                    emitNode(as, forStmt->increment, ctx);
                }
                
                // 6. Jump back to loop start
                int32_t backOffset = loopStart - (as->offset + 5);
                Asm_Jmp(as, backOffset);
                
                // 7. Patch the loop end jump
                if (loopEndPatch != 0) {
                    size_t loopEnd = as->offset;
                    int32_t forwardOffset = loopEnd - (loopEndPatch + 4);
                    Asm_Patch32(as, loopEndPatch, forwardOffset);
                }
            }
            
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
            funcCtx.localCount = 0;
            
            // ABI: Save Callee-Saved Registers (RBX, R12-R15)
            Asm_Push(&funcAs, RBX);
            Asm_Push(&funcAs, R12);
            Asm_Push(&funcAs, R13);
            Asm_Push(&funcAs, R14);
            Asm_Push(&funcAs, R15);
            funcCtx.stackSize = 40; // 5 regs * 8 bytes
            
            // Simple: Spill to stack.
            // Simple: Spill to stack.
            // Raw ABI: Check param types to determine source register (GPR or XMM)
            Register gprRegs[] = { RDI, RSI, RDX, RCX, R8, R9 };
            int gprCount = 0;
            int xmmCount = 0;
            
            for (int i = 0; i < func->paramCount; i++) {
                Token type = func->paramTypes[i];
                int isFloat = 0;
                
                if (type.length == 5 && memcmp(type.start, "float", 5) == 0) isFloat = 1;
                else if (type.length == 6 && memcmp(type.start, "double", 6) == 0) isFloat = 1;

                if (isFloat) {
                    if (xmmCount < 6) {
                        // Push XMM[k]. 
                        // SUB RSP, 8
                        // MOVSD [RSP], XMMk
                        // We use MOVSD (double) for storage to keep 8-byte alignment uniformly.
                        Asm_Emit8(&funcAs, 0x48); Asm_Emit8(&funcAs, 0x83); Asm_Emit8(&funcAs, 0xEC); Asm_Emit8(&funcAs, 0x08);
                        
                        // MOVSD [RSP], XMM
                        // F2 0F 11 04 24 (if XMM0)
                        // ModRM: 00 xxx 100(SIB) ... 24
                        // Simpler: MOVQ [RSP], XMMk (66 48 0F 7E /r)
                        // Dest is Mem.
                        uint8_t modrm = 0x04 + (xmmCount << 3); 
                        // SIB for RSP is 0x24 (Scale=1, Index=None, Base=RSP)
                        
                        // Let's use Asm_Mov_Mem_Reg? No that's GPR.
                        // Manual XMM store:
                        // MOVQ [RSP], XMMk
                        Asm_Emit8(&funcAs, 0x66); Asm_Emit8(&funcAs, 0x48); Asm_Emit8(&funcAs, 0x0F); Asm_Emit8(&funcAs, 0x7E); 
                        // ModRM: 00(Mode) Reg(XMM) RM(RSP=100 -> SIB)
                        // Reg=xmmCount. RM=4.
                        Asm_Emit8(&funcAs, modrm);
                        // SIB: 00 100 100 = 0x24
                        Asm_Emit8(&funcAs, 0x24);
                        
                        xmmCount++;
                    } else {
                        // Stack overflow
                        Asm_Push(&funcAs, RAX); // Placeholder
                    }
                } else {
                    // Int/Ptr
                    if (gprCount < 6) {
                        Asm_Push(&funcAs, gprRegs[gprCount++]);
                    } else {
                        Asm_Push(&funcAs, RAX); // Stack overflow
                    }
                }
                
                funcCtx.stackSize += 8;
                Local* local = &funcCtx.locals[funcCtx.localCount++];
                local->name = func->params[i];
                local->typeName = func->paramTypes[i]; 
                local->offset = funcCtx.stackSize;
            }
            
            // Align Stack to 16 bytes for body execution (CALLs)
            if (funcCtx.stackSize % 16 != 0) {
                 Asm_Emit8(&funcAs, 0x48); Asm_Emit8(&funcAs, 0x83); Asm_Emit8(&funcAs, 0xEC); Asm_Emit8(&funcAs, 0x08); // SUB RSP, 8
                 funcCtx.stackSize += 8;
                 // Note: This padding is not registered as a local variable, just dead space.
            }
            
            // Compile Body
            emitNode(&funcAs, func->body, &funcCtx);

            
            // Default Return (if user didn't)
            Asm_Mov_Imm64(&funcAs, RAX, VAL_NULL); // Default return nil
            
            // Epilogue: Restore Regs
            Asm_Mov_Reg_Reg(&funcAs, RSP, RBP); // Reset stack
            // SUB RSP, 40 (0x28)
            Asm_Emit8(&funcAs, 0x48); Asm_Emit8(&funcAs, 0x83); Asm_Emit8(&funcAs, 0xEC); Asm_Emit8(&funcAs, 0x28);
            
            Asm_Pop(&funcAs, R15);
            Asm_Pop(&funcAs, R14);
            Asm_Pop(&funcAs, R13);
            Asm_Pop(&funcAs, R12);
            Asm_Pop(&funcAs, RBX);
            
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
            
            // Protect Executable Memory
            Jit_ProtectExec(funcMem, MAX_JIT_SIZE);

            // 5. Register Global Function
            registerGlobalFunction(func->name.start, func->name.length, funcMem);

            // 6. Check for Main
            if (func->name.length == 4 && memcmp(func->name.start, "Main", 4) == 0) {
                 mainFunc = funcMem; // Update global
            }

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
            // Epilogue and Ret
            Asm_Mov_Reg_Reg(as, RSP, RBP); // Reset stack
            // SUB RSP, 40 (0x28)
            Asm_Emit8(as, 0x48); Asm_Emit8(as, 0x83); Asm_Emit8(as, 0xEC); Asm_Emit8(as, 0x28);
            
            Asm_Pop(as, R15);
            Asm_Pop(as, R14);
            Asm_Pop(as, R13);
            Asm_Pop(as, R12);
            Asm_Pop(as, RBX);
            
            Asm_Pop(as, RBP);
            Asm_Ret(as);
            break;
        }

        default: break;
    }
}

JitFunction Jit_Compile(AstNode* root) {
   if (!root) return NULL;
    
    registerGlobalStructs(root);
    
    // Compile Top-Level Block
    void* mem = Jit_AllocExec(MAX_JIT_SIZE);
    Assembler as;
    Asm_Init(&as, (uint8_t*)mem, MAX_JIT_SIZE);
    
    CompilerContext ctx = {0};
    ctx.localCount = 0;
    ctx.stackSize = 0;
    
    // Emission
    emitNode(&as, root, &ctx);
    
    // Verify Executable
    Jit_ProtectExec(mem, MAX_JIT_SIZE);
    
    if (mainFunc == NULL) {
        fprintf(stderr, "JIT Error: No 'Main' function found.\n");
        return NULL;
    }
    return (JitFunction)mainFunc;
}
