#include "Jit/AssemblerX64.h"
#include <stdio.h>
#include <stdlib.h>

void Asm_Init(Assembler* as, uint8_t* buffer, size_t capacity) {
    as->buffer = buffer;
    as->capacity = capacity;
    as->offset = 0;
}

void Asm_Emit8(Assembler* as, uint8_t byte) {
    if (as->offset >= as->capacity) {
        fprintf(stderr, "[Vanarize Assembler] Error: Buffer overflow\n");
        exit(1);
    }
    as->buffer[as->offset++] = byte;
}

// MOV r64, imm64
// Opcode: 48 B8+rd val (for RAX..RDI)
void Asm_Mov_Imm64(Assembler* as, Register dst, uint64_t val) {
    if (dst > RDI) {
        // REX.W + REX.B prefix needed for R8-R15
        // Todo implementation
        fprintf(stderr, "Extended registers not yet supported in Mov_Imm64\n");
        exit(1);
    }
    // REX.W prefix (48) is strictly speaking needed for 64-bit operands,
    // but the short form 48 B8+rd is common.
    // Actually: REX.W (0x48) | B8+rd
    Asm_Emit8(as, 0x48);
    Asm_Emit8(as, 0xB8 + dst); 
    
    // Emit 64-bit immediate
    for (int i = 0; i < 8; i++) {
        Asm_Emit8(as, (uint8_t)(val & 0xFF));
        val >>= 8;
    }
}

// MOV dst, src
// Opcode: 48 89 /r (ModR/M)
void Asm_Mov_Reg_Reg(Assembler* as, Register dst, Register src) {
    if (dst > RDI || src > RDI) {
        fprintf(stderr, "Extended registers not yet supported in Mov_Reg_Reg\n");
        exit(1);
    }
    Asm_Emit8(as, 0x48);
    Asm_Emit8(as, 0x89);
    // Mod = 11 (Register) | Src | Dst
    uint8_t modrm = 0xC0 | (src << 3) | dst;
    Asm_Emit8(as, modrm);
}

// ADD dst, src
// Opcode: 48 01 /r
void Asm_Add_Reg_Reg(Assembler* as, Register dst, Register src) {
    if (dst > RDI || src > RDI) {
        fprintf(stderr, "Extended registers not yet supported in Add_Reg_Reg\n");
        exit(1);
    }
    // REX.W (48) 
    Asm_Emit8(as, 0x48);
    Asm_Emit8(as, 0x01);
    
    // ModR/M Byte
    // Mode 11 (Register Addressing) | Src (3 bits) | Dst (3 bits)
    // 0xC0 + (src << 3) + dst
    uint8_t modrm = 0xC0 | (src << 3) | dst;
    Asm_Emit8(as, modrm);
}

// PUSH r64
// Opcode: 50 + rd
void Asm_Push(Assembler* as, Register src) {
    if (src > RDI) {
        fprintf(stderr, "Extended registers not yet supported in Push\n");
        exit(1);
    }
    Asm_Emit8(as, 0x50 + src);
}

// POP r64
// Opcode: 58 + rd
void Asm_Pop(Assembler* as, Register dst) {
    if (dst > RDI) {
        fprintf(stderr, "Extended registers not yet supported in Pop\n");
        exit(1);
    }
    Asm_Emit8(as, 0x58 + dst);
}

// CALL r64
// Opcode: FF /2 (ModR/M with reg field=2)
// ModR/M for register: 11 010 reg -> 0xD0 + reg
void Asm_Call_Reg(Assembler* as, Register src) {
    if (src > RDI) {
        fprintf(stderr, "Extended registers not yet supported in Call\n");
        exit(1);
    }
    Asm_Emit8(as, 0xFF);
    Asm_Emit8(as, 0xD0 + src);
}

void Asm_Mov_Reg_Ptr(Assembler* as, Register dst, void* ptr) {
    Asm_Mov_Imm64(as, dst, (uint64_t)(uintptr_t)ptr);
}

// Helper for ModR/M Disp32
static void emitModRM_Disp32(Assembler* as, Register reg, Register base, int32_t offset) {
    // Mod = 10 (Disp32) | Reg | R/M
    Asm_Emit8(as, 0x80 | (reg << 3) | base);
    // Emit 32-bit offset (little endian)
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(offset & 0xFF));
        offset >>= 8;
    }
}

// MOV dst, [base + offset]
// Opcode: 48 8B /r
void Asm_Mov_Reg_Mem(Assembler* as, Register dst, Register base, int32_t offset) {
    Asm_Emit8(as, 0x48);
    Asm_Emit8(as, 0x8B);
    emitModRM_Disp32(as, dst, base, offset);
}

// MOV [base + offset], src
// Opcode: 48 89 /r
void Asm_Mov_Mem_Reg(Assembler* as, Register base, int32_t offset, Register src) {
    Asm_Emit8(as, 0x48);
    Asm_Emit8(as, 0x89);
    emitModRM_Disp32(as, src, base, offset);
}

// CMP r64, imm32
// Opcode: 48 81 /7 id
void Asm_Cmp_Reg_Imm(Assembler* as, Register dst, int32_t imm) {
    Asm_Emit8(as, 0x48);
    Asm_Emit8(as, 0x81);
    Asm_Emit8(as, 0xF8 + dst); // ModRM(11, 111, reg) -> F8+reg? No.
    // /7 means REG field is 111.
    // Mod=11 (Register mode) | REG=111 | R/M=dst
    // 11 111 dst -> 0xF8 + dst
    // Wait, ModRM is Mod(2) Reg(3) RM(3).
    // 11 111 dst(=0) -> 11111000 = F8. Correct.
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(imm & 0xFF));
        imm >>= 8;
    }
}

// JMP rel32
// Opcode: E9 cd
void Asm_Jmp(Assembler* as, int32_t offset) {
    Asm_Emit8(as, 0xE9);
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(offset & 0xFF));
        offset >>= 8;
    }
}

// JE rel32
// Opcode: 0F 84 cd
void Asm_Je(Assembler* as, int32_t offset) {
    Asm_Emit8(as, 0x0F);
    Asm_Emit8(as, 0x84);
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(offset & 0xFF));
        offset >>= 8;
    }
}

// JNE rel32
// Opcode: 0F 85 cd
void Asm_Jne(Assembler* as, int32_t offset) {
    Asm_Emit8(as, 0x0F);
    Asm_Emit8(as, 0x85);
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(offset & 0xFF));
        offset >>= 8;
    }
}

void Asm_Patch32(Assembler* as, size_t offset, int32_t value) {
    if (offset + 4 > as->capacity) return; // Error
    as->buffer[offset] = (uint8_t)(value & 0xFF);
    as->buffer[offset+1] = (uint8_t)((value >> 8) & 0xFF);
    as->buffer[offset+2] = (uint8_t)((value >> 16) & 0xFF);
    as->buffer[offset+3] = (uint8_t)((value >> 24) & 0xFF);
}

// RET
// Opcode: C3
void Asm_Ret(Assembler* as) {
    Asm_Emit8(as, 0xC3);
}
