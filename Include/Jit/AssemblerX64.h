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

// ADD dst, src
void Asm_Add_Reg_Reg(Assembler* as, Register dst, Register src);

// PUSH r64
void Asm_Push(Assembler* as, Register src);

// POP r64
void Asm_Pop(Assembler* as, Register dst);

// RET
void Asm_Ret(Assembler* as);

#endif // VANARIZE_JIT_ASSEMBLER_H
