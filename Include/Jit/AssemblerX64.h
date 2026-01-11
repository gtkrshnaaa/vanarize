#ifndef VANARIZE_JIT_ASSEMBLER_H
#define VANARIZE_JIT_ASSEMBLER_H

#include <stdint.h>
#include <stddef.h>

/**
 * X64 Register Enums
 * Mapping to internal 3-bit encodings
 */
typedef enum {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    // R8-R15 require REX prefix, handling later for simplicity
    R8 = 8, R9 = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
} Register;

typedef struct {
    uint8_t* buffer;
    size_t capacity;
    size_t offset;
} Assembler;

// Initialize assembler with a buffer
void Asm_Init(Assembler* as, uint8_t* buffer, size_t capacity);

// Emit single byte
void Asm_Emit8(Assembler* as, uint8_t byte);

// Emit specific instructions
// MOV dst, imm64
void Asm_Mov_Imm64(Assembler* as, Register dst, uint64_t val);

// MOV dst, src
void Asm_Mov_Reg_Reg(Assembler* as, Register dst, Register src);

// ADD dst, src (64-bit register add)
void Asm_Add_Reg_Reg(Assembler* as, Register dst, Register src);

// ADD r64, imm32 (Integer specialization - with INC optimization)
void Asm_Add_Reg_Imm(Assembler* as, Register dst, int32_t imm);

// AND dst, src
void Asm_And_Reg_Reg(Assembler* as, Register dst, Register src);

// PUSH r64
void Asm_Push(Assembler* as, Register src);

// POP r64
void Asm_Pop(Assembler* as, Register dst);

// CALL r64 (Absolute call)
void Asm_Call_Reg(Assembler* as, Register src);

// MOV dst, imm64 (Pointer version)
void Asm_Mov_Reg_Ptr(Assembler* as, Register dst, void* ptr);

// MOV dst, [base + offset]
void Asm_Mov_Reg_Mem(Assembler* as, Register dst, Register base, int32_t offset);

// MOV [base + offset], src
void Asm_Mov_Mem_Reg(Assembler* as, Register base, int32_t offset, Register src);

// Integer Arithmetic (64-bit ALU)
void Asm_Sub_Reg_Reg_64(Assembler* as, Register dst, Register src);
void Asm_Imul_Reg_Reg_64(Assembler* as, Register dst, Register src);
void Asm_Inc_Reg(Assembler* as, Register reg);
void Asm_Dec_Reg(Assembler* as, Register reg);

// CMP reg, imm64 (Only supports 32-bit immediate for now for simplicity or full?)
// CMP r64, r64 is also useful.
// Let's do CMP r64, imm32 (sign extended) is standard '48 81 /7 id'
void Asm_Cmp_Reg_Imm(Assembler* as, Register dst, int32_t imm);

// CMP r64, r64
void Asm_Cmp_Reg_Reg(Assembler* as, Register dst, Register src);

// Jcc rel32
void Asm_Je(Assembler* as, int32_t offset);
void Asm_Jne(Assembler* as, int32_t offset);
void Asm_Jmp(Assembler* as, int32_t offset);
void Asm_Jae(Assembler* as, int32_t offset);  // Jump if Above or Equal (unsigned >=)
void Asm_Jge(Assembler* as, int32_t offset);  // Jump if Greater or Equal (signed >=)
void Asm_Jl(Assembler* as, int32_t offset);   // Jump if Less (signed <)

// Patching
void Asm_Patch32(Assembler* as, size_t offset, int32_t value);

// RET
void Asm_Ret(Assembler* as);

#endif // VANARIZE_JIT_ASSEMBLER_H
