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
    (void)as; (void)dst; (void)src;
    // Implementation placeholder if needed, focusing on Imm -> Reg -> Ret first
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

// RET
// Opcode: C3
void Asm_Ret(Assembler* as) {
    Asm_Emit8(as, 0xC3);
}
