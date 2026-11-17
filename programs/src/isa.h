#ifndef BNDETC_ISA_H
#define BNDETC_ISA_H

#include "common.h"

typedef enum {
    OP_ADD, OP_AND, OP_EQ, OP_MOVI, OP_ADDI, OP_OR, OP_LT, OP_LOAD,
    OP_JMP, OP_NOT, OP_JZ, OP_STORE, OP_STOP, OP_RESERVED, OP_ASSERT, OP_GUESS
} Opcode;

typedef struct {
    Opcode opcode;
    unsigned rd, ra, rb;
    uint16_t payload;
} Instruction;

extern const char *const opcode_names[16];
uint32_t encode_instruction(Instruction instruction);
bool decode_instruction(uint32_t word, Instruction *instruction);
int32_t signed_word(uint16_t word);

typedef struct {
    uint32_t *code;
    uint16_t *input;
    unsigned char *export_private;
    size_t code_capacity;
    uint32_t n, i, m;
    uint16_t export_registers;
    uint64_t fingerprint;
} Image;

bool image_load(Image *image, const char *path, char error[256], bool *resource_error);
void image_free(Image *image);

#endif
