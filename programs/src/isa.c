#include "isa.h"

const char *const opcode_names[16] = {
    "ADD", "AND", "EQ", "MOVI", "ADDI", "OR", "LT", "LOAD",
    "JMP", "NOT", "JZ", "STORE", "STOP", "RESERVED", "ASSERT", "GUESS"
};

uint32_t encode_instruction(Instruction instruction)
{
    return ((uint32_t)instruction.opcode << 28) | ((uint32_t)instruction.rd << 24) |
           ((uint32_t)instruction.ra << 20) | ((uint32_t)instruction.rb << 16) |
           instruction.payload;
}

bool decode_instruction(uint32_t word, Instruction *instruction)
{
    const uint32_t rd = UINT32_C(0x0f000000);
    const uint32_t ra = UINT32_C(0x00f00000);
    const uint32_t rb = UINT32_C(0x000f0000);
    const uint32_t payload = UINT32_C(0x0000ffff);
    const uint32_t masks[16] = {
        rd | ra | rb, rd | ra | rb, rd | ra | rb, rd | payload,
        rd | ra | payload, rd | ra | rb, rd | ra | rb, rd | ra,
        0, rd | ra, ra | payload, ra | rb, 0, 0, ra, rd
    };
    Opcode opcode = (Opcode)(word >> 28);
    unsigned mode = (word >> 16) & 15u;
    uint32_t mask = masks[opcode];
    if (opcode == OP_RESERVED)
        return false;
    if (opcode == OP_JMP) {
        if (mode > 1)
            return false;
        mask = mode == 0 ? payload : ra | rb;
    }
    if ((word & UINT32_C(0x0fffffff)) & ~mask)
        return false;
    *instruction = (Instruction){
        opcode, (word >> 24) & 15u, (word >> 20) & 15u, mode, (uint16_t)word
    };
    return true;
}

int32_t signed_word(uint16_t word)
{
    return word < 32768u ? (int32_t)word : (int32_t)word - 65536;
}
