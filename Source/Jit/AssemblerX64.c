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
    // REX.W prefix (48) is strictly speaking needed for 64-bit operands.
    // Extended regs need REX.B (0x01).
    uint8_t rex = 0x48;
    if (dst >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0xB8 + (dst & 7)); 
    
    // Emit 64-bit immediate
    for (int i = 0; i < 8; i++) {
        Asm_Emit8(as, (uint8_t)(val & 0xFF));
        val >>= 8;
    }
}

// MOV dst, src
// Opcode: 48 89 /r (ModR/M)
void Asm_Mov_Reg_Reg(Assembler* as, Register dst, Register src) {
    // REX Prefix logic
    uint8_t rex = 0x48;
    if (src >= R8) rex |= 0x04; // REX.R (Source is Reg field)
    if (dst >= R8) rex |= 0x01; // REX.B (Dest is RM field)
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x89);
    
    // Mod = 11 (Register) | Src | Dst
    uint8_t srcEnc = src & 7;
    uint8_t dstEnc = dst & 7;
    uint8_t modrm = 0xC0 | (srcEnc << 3) | dstEnc;
    Asm_Emit8(as, modrm);
}

// ADD dst, src
// Opcode: 48 01 /r
void Asm_Add_Reg_Reg(Assembler* as, Register dst, Register src) {
    // REX Prefix logic
    uint8_t rex = 0x48;
    if (src >= R8) rex |= 0x04;
    if (dst >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x01);
    
    uint8_t srcEnc = src & 7;
    uint8_t dstEnc = dst & 7;
    uint8_t modrm = 0xC0 | (srcEnc << 3) | dstEnc;
    Asm_Emit8(as, modrm);
}

// AND dst, src
// Opcode: 48 21 /r
void Asm_And_Reg_Reg(Assembler* as, Register dst, Register src) {
    // REX Prefix logic
    uint8_t rex = 0x48;
    if (src >= R8) rex |= 0x04;
    if (dst >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x21); // AND opcode
    
    uint8_t srcEnc = src & 7;
    uint8_t dstEnc = dst & 7;
    uint8_t modrm = 0xC0 | (srcEnc << 3) | dstEnc;
    Asm_Emit8(as, modrm);
}

// ==================== INTEGER SPECIALIZATION INSTRUCTIONS ====================

// ADD r64, imm32 (with INC optimization for imm=1)
// Opcode: 48 81 /0 id (ADD), or 48 FF /0 (INC)
void Asm_Add_Reg_Imm(Assembler* as, Register dst, int32_t imm) {
    // Optimization: ADD r64, 1 -> INC r64 (1 byte shorter, same speed)
    if (imm == 1) {
        Asm_Inc_Reg(as, dst);
        return;
    }
    
    // REX.W prefix
    uint8_t rex = 0x48;
    if (dst >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x81);  // ADD r/m64, imm32
    
    // ModRM: Mod=11 (register), Reg=/0, RM=dst
    uint8_t modrm = 0xC0 | (dst & 7);
    Asm_Emit8(as, modrm);
    
    // Emit imm32 (little-endian)
    for (int i = 0; i < 4; i++) {
        Asm_Emit8(as, (uint8_t)(imm & 0xFF));
        imm >>= 8;
    }
}

// SUB r64, r64
// Opcode: 48 29 /r
void Asm_Sub_Reg_Reg_64(Assembler* as, Register dst, Register src) {
    uint8_t rex = 0x48;
    if (src >= R8) rex |= 0x04;
    if (dst >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x29);  // SUB opcode
    
    uint8_t srcEnc = src & 7;
    uint8_t dstEnc = dst & 7;
    uint8_t modrm = 0xC0 | (srcEnc << 3) | dstEnc;
    Asm_Emit8(as, modrm);
}

// IMUL r64, r64 (Two-operand signed multiply)
// Opcode: 48 0F AF /r
void Asm_Imul_Reg_Reg_64(Assembler* as, Register dst, Register src) {
    uint8_t rex = 0x48;
    if (dst >= R8) rex |= 0x04;  // Note: dst is in REG field for IMUL
    if (src >= R8) rex |= 0x01;  // src is in R/M field
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x0F);  // Two-byte opcode prefix
    Asm_Emit8(as, 0xAF);  // IMUL opcode
    
    uint8_t dstEnc = dst & 7;
    uint8_t srcEnc = src & 7;
    uint8_t modrm = 0xC0 | (dstEnc << 3) | srcEnc;
    Asm_Emit8(as, modrm);
}

// INC r64
// Opcode: 48 FF /0
void Asm_Inc_Reg(Assembler* as, Register reg) {
    uint8_t rex = 0x48;
    if (reg >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0xFF);
    Asm_Emit8(as, 0xC0 | (reg & 7));  // ModRM: /0 extension
}

// DEC r64
// Opcode: 48 FF /1
void Asm_Dec_Reg(Assembler* as, Register reg) {
    uint8_t rex = 0x48;
    if (reg >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0xFF);
    Asm_Emit8(as, 0xC8 | (reg & 7));  // ModRM: /1 extension
}

// PUSH r64
// Opcode: 50 + rd
void Asm_Push(Assembler* as, Register src) {
    if (src >= R8) {
        // REX.B (41) + 50+(src-8)
        // Actually REX is 0x41 (0100 0001) - W bit not needed for Push/Pop usually?
        // Intel manual: PUSH r64. default 64-bit size. REX.W not needed.
        // But REX.B is needed for extended reg.
        // So 0x41.
        Asm_Emit8(as, 0x41);
        Asm_Emit8(as, 0x50 + (src & 7));
    } else {
        Asm_Emit8(as, 0x50 + src);
    }
}

// POP r64
// Opcode: 58 + rd
void Asm_Pop(Assembler* as, Register dst) {
    if (dst >= R8) {
        // REX.B (41)
        Asm_Emit8(as, 0x41);
        Asm_Emit8(as, 0x58 + (dst & 7));
    } else {
        Asm_Emit8(as, 0x58 + dst);
    }
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
// Helper for ModR/M Disp32
static void emitModRM_Disp32(Assembler* as, Register reg, Register base, int32_t offset) {
    // Mod = 10 (Disp32) | Reg | R/M
    Asm_Emit8(as, 0x80 | (reg << 3) | (base & 7));
    
    // Check for SIB requirement (RSP=4 or R12=12)
    if ((base & 7) == 4) {
        // SIB: Scale=0 (00), Index=None (100/4), Base=RSP (100/4)
        // 0x24
        Asm_Emit8(as, 0x24);
    }
    
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
    // CMP r64, imm32: 48 81 /7 id
    Asm_Emit8(as, 0x48); // REX.W
    Asm_Emit8(as, 0x81); // CMP opcode
    // ModRM: Mod=11 (direct), Reg=111 (/7), RM=dst
    uint8_t modrm = 0xF8 + dst;
    Asm_Emit8(as, modrm);
    
    // Emit imm32 (little-endian)
    Asm_Emit8(as, imm & 0xFF);
    Asm_Emit8(as, (imm >> 8) & 0xFF);
    Asm_Emit8(as, (imm >> 16) & 0xFF);
    Asm_Emit8(as, (imm >> 24) & 0xFF);
}

// CMP r64, r64
void Asm_Cmp_Reg_Reg(Assembler* as, Register dst, Register src) {
    // REX Prefix logic
    uint8_t rex = 0x48;
    if (src >= R8) rex |= 0x04;
    if (dst >= R8) rex |= 0x01;
    
    Asm_Emit8(as, rex);
    Asm_Emit8(as, 0x39); // CMP opcode
    
    uint8_t srcEnc = src & 7;
    uint8_t dstEnc = dst & 7;
    uint8_t modrm = 0xC0 | (srcEnc << 3) | dstEnc;
    Asm_Emit8(as, modrm);
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

// JAE rel32 (Jump if Above or Equal - unsigned >=)
// Opcode: 0F 83 cd
void Asm_Jae(Assembler* as, int32_t offset) {
    Asm_Emit8(as, 0x0F);
    Asm_Emit8(as, 0x83);
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(offset & 0xFF));
        offset >>= 8;
    }
}

// JGE rel32 (Jump if Greater or Equal - signed >=)
// Opcode: 0F 8D cd
void Asm_Jge(Assembler* as, int32_t offset) {
    Asm_Emit8(as, 0x0F);
    Asm_Emit8(as, 0x8D);
    for (int i=0; i<4; i++) {
        Asm_Emit8(as, (uint8_t)(offset & 0xFF));
        offset >>= 8;
    }
}

// JL rel32 (Jump if Less - signed <)
// Opcode: 0F 8C cd
void Asm_Jl(Assembler* as, int32_t offset) {
    Asm_Emit8(as, 0x0F);
    Asm_Emit8(as, 0x8C);
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

// ==================== AVX SIMD INSTRUCTIONS ====================
// VEX Prefix Format (3-byte): C4 RXBm-mmmm WvvvvLpp
// VEX Prefix Format (2-byte): C5 RvvvvLpp (when R=1, X=1, B=1, m-mmmm=00001)

// Helper: Emit 2-byte VEX prefix
// R=1, vvvv=src1 inverted, L=1 (256-bit), pp=01 (66 prefix equivalent)
static void emitVex2(Assembler* as, YmmRegister vvvv, int L, int pp) {
    // C5 RvvvvLpp
    // R=1 (no REX.R), vvvv inverted, L=1 for 256-bit
    uint8_t byte2 = 0x80 | ((~vvvv & 0xF) << 3) | (L << 2) | pp;
    Asm_Emit8(as, 0xC5);
    Asm_Emit8(as, byte2);
}

// Helper: Emit 3-byte VEX prefix for AVX2
static void emitVex3(Assembler* as, int R, int X, int B, int mmmmm, int W, YmmRegister vvvv, int L, int pp) {
    // C4 RXBm-mmmm WvvvvLpp
    uint8_t byte2 = ((~R & 1) << 7) | ((~X & 1) << 6) | ((~B & 1) << 5) | (mmmmm & 0x1F);
    uint8_t byte3 = ((W & 1) << 7) | ((~vvvv & 0xF) << 3) | ((L & 1) << 2) | (pp & 3);
    Asm_Emit8(as, 0xC4);
    Asm_Emit8(as, byte2);
    Asm_Emit8(as, byte3);
}

// VXORPD ymm, ymm, ymm - Zero a YMM register
// VEX.256.66.0F.WIG 57 /r
void Asm_Vxorpd_Ymm(Assembler* as, YmmRegister dst, YmmRegister src1, YmmRegister src2) {
    emitVex2(as, src1, 1, 1);  // L=1 (256-bit), pp=01 (66)
    Asm_Emit8(as, 0x57);       // XORPD opcode
    Asm_Emit8(as, 0xC0 | (dst << 3) | src2);  // ModR/M: 11 dst src2
}

// VPXOR ymm, ymm, ymm - Integer XOR (for zeroing int vectors)
// VEX.256.66.0F.WIG EF /r
void Asm_Vpxor_Ymm(Assembler* as, YmmRegister dst, YmmRegister src1, YmmRegister src2) {
    emitVex2(as, src1, 1, 1);  // L=1 (256-bit), pp=01 (66)
    Asm_Emit8(as, 0xEF);       // PXOR opcode
    Asm_Emit8(as, 0xC0 | (dst << 3) | src2);
}

// VPADDD ymm, ymm, ymm - Add 8 packed 32-bit integers
// VEX.256.66.0F.WIG FE /r
void Asm_Vpaddd_Ymm(Assembler* as, YmmRegister dst, YmmRegister src1, YmmRegister src2) {
    emitVex2(as, src1, 1, 1);  // L=1 (256-bit), pp=01 (66)
    Asm_Emit8(as, 0xFE);       // PADDD opcode
    Asm_Emit8(as, 0xC0 | (dst << 3) | src2);
}

// VADDPD ymm, ymm, ymm - Add 4 packed doubles
// VEX.256.66.0F.WIG 58 /r
void Asm_Vaddpd_Ymm(Assembler* as, YmmRegister dst, YmmRegister src1, YmmRegister src2) {
    emitVex2(as, src1, 1, 1);  // L=1 (256-bit), pp=01 (66)
    Asm_Emit8(as, 0x58);       // ADDPD opcode
    Asm_Emit8(as, 0xC0 | (dst << 3) | src2);
}

// VMOVDQU ymm, [RIP + disp] - Load 256 bits unaligned
// VEX.256.F3.0F.WIG 6F /r
void Asm_Vmovdqu_Ymm_Mem(Assembler* as, YmmRegister dst, void* mem) {
    // Use RIP-relative addressing would be complex, use absolute for now
    // Load pointer to RAX, then use [RAX]
    // Actually simpler: emit address inline after instruction
    // For now, stub - we'll load from static data
    (void)as; (void)dst; (void)mem;
    // TODO: Implement properly with RIP-relative or static data section
}

// VMOVDQU [mem], ymm - Store 256 bits unaligned
void Asm_Vmovdqu_Mem_Ymm(Assembler* as, void* mem, YmmRegister src) {
    (void)as; (void)mem; (void)src;
    // TODO: Implement properly
}

// Horizontal sum of 8 ints in YMM -> RAX
// Uses VEXTRACTI128, VPADDD, VPHADDD sequence
void Asm_Avx_HSum_Int(Assembler* as, YmmRegister src) {
    // For now, simple horizontal sum using SSE after extract
    // VEXTRACTI128 xmm1, ymm0, 1  ; Get high 128 bits to XMM1
    // VEX.256.66.0F3A.W0 39 /r ib
    emitVex3(as, 1, 1, 1, 0x03, 0, YMM0, 1, 1);  // 0F3A map
    Asm_Emit8(as, 0x39);  // VEXTRACTI128
    Asm_Emit8(as, 0xC1 | (src << 3));  // ModR/M: ymm0 -> xmm1
    Asm_Emit8(as, 0x01);  // imm8 = 1 (high half)
    
    // VPADDD xmm0, xmm0, xmm1 (128-bit)
    emitVex2(as, YMM0, 0, 1);  // L=0 (128-bit)
    Asm_Emit8(as, 0xFE);
    Asm_Emit8(as, 0xC1);  // xmm0 += xmm1
    
    // Now xmm0 has 4 ints, need to sum to 1
    // VPHADDD xmm0, xmm0, xmm0 (twice)
    // VEX.128.66.0F38.WIG 02 /r
    emitVex3(as, 1, 1, 1, 0x02, 0, YMM0, 0, 1);  // 0F38 map
    Asm_Emit8(as, 0x02);  // VPHADDD
    Asm_Emit8(as, 0xC0);  // xmm0, xmm0
    
    emitVex3(as, 1, 1, 1, 0x02, 0, YMM0, 0, 1);
    Asm_Emit8(as, 0x02);
    Asm_Emit8(as, 0xC0);
    
    // VMOVD eax, xmm0
    // VEX.128.66.0F.W0 7E /r
    emitVex2(as, YMM0, 0, 1);
    Asm_Emit8(as, 0x7E);
    Asm_Emit8(as, 0xC0);  // xmm0 -> eax
}

// Horizontal sum of 4 doubles in YMM -> XMM0 (single double)
void Asm_Avx_HSum_Double(Assembler* as, YmmRegister src) {
    // VEXTRACTF128 xmm1, ymm0, 1  ; Get high 128 bits
    // VEX.256.66.0F3A.W0 19 /r ib
    emitVex3(as, 1, 1, 1, 0x03, 0, YMM0, 1, 1);
    Asm_Emit8(as, 0x19);  // VEXTRACTF128
    Asm_Emit8(as, 0xC1 | (src << 3));
    Asm_Emit8(as, 0x01);  // imm8 = 1
    
    // VADDPD xmm0, xmm0, xmm1 (128-bit)
    emitVex2(as, YMM0, 0, 1);  // L=0 (128-bit)
    Asm_Emit8(as, 0x58);
    Asm_Emit8(as, 0xC1);  // xmm0 += xmm1
    
    // VHADDPD xmm0, xmm0, xmm0
    // VEX.128.66.0F.WIG 7C /r
    emitVex2(as, YMM0, 0, 1);
    Asm_Emit8(as, 0x7C);  // HADDPD
    Asm_Emit8(as, 0xC0);  // xmm0, xmm0
    
    // Result is now in low 64 bits of XMM0
}
