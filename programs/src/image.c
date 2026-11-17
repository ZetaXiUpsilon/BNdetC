#include "isa.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[128];
    uint32_t pc;
    size_t line;
} Label;

typedef struct {
    Reader *reader;
    Image *image;
    Token token;
    int token_status;
    bool resource_error;
    Label *labels;
    Label *fixups;
    size_t label_count, label_capacity;
    size_t fixup_count, fixup_capacity;
} Assembler;

enum { PSEUDO_MOV = 16, PSEUDO_SUB, PSEUDO_INC, PSEUDO_DEC };

static bool reg_token(const char *text, unsigned *reg)
{
    uint64_t index;
    if (toupper((unsigned char)text[0]) != 'R' ||
        !parse_uint(text + 1, 11, &index))
        return false;
    *reg = (unsigned)index;
    return true;
}

static bool label_token(const char *text)
{
    unsigned char first = (unsigned char)text[0];
    if (!isalpha(first) && first != '_')
        return false;
    if ((toupper(first) == 'R' || toupper(first) == 'S') &&
        text[1] && strspn(text + 1, "0123456789") == strlen(text + 1))
        return false;
    for (size_t i = 1; text[i]; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (!isalnum(ch) && ch != '_' && ch != '.')
            return false;
    }
    return true;
}

static void next_token(Assembler *assembler)
{
    assembler->token_status = reader_next(assembler->reader, &assembler->token);
}

static bool take(Assembler *assembler, const char *text)
{
    if (assembler->token_status != 1 || !equal_keyword(assembler->token.text, text))
        return reader_error(assembler->reader, assembler->token.line, "expected '%s'", text);
    next_token(assembler);
    return true;
}

static bool take_reg(Assembler *assembler, unsigned *reg)
{
    if (assembler->token_status != 1 || !reg_token(assembler->token.text, reg))
        return reader_error(assembler->reader, assembler->token.line, "expected R0 through R11");
    next_token(assembler);
    return true;
}

static bool add_label(Assembler *assembler, const Token *token, bool fixup)
{
    Label **items = fixup ? &assembler->fixups : &assembler->labels;
    size_t *count = fixup ? &assembler->fixup_count : &assembler->label_count;
    size_t *capacity = fixup ? &assembler->fixup_capacity : &assembler->label_capacity;
    void *storage = *items;
    if (*count == WORD_SPACE)
        return reader_error(assembler->reader, token->line, "too many labels or references");
    if (!grow_array(&storage, capacity, *count + 1, sizeof(Label), WORD_SPACE)) {
        assembler->resource_error = true;
        return reader_error(assembler->reader, token->line, "cannot allocate assembler labels");
    }
    *items = storage;
    Label *label = &(*items)[(*count)++];
    strcpy(label->name, token->text);
    label->line = token->line;
    label->pc = assembler->image->n;
    return true;
}

static bool take_value(Assembler *assembler, uint16_t *value, bool address, bool labels)
{
    const Token *token = &assembler->token;
    if (assembler->token_status != 1)
        return reader_error(assembler->reader, token->line, "missing value");
    if (!(address ? parse_address(token->text, value) : parse_word(token->text, value))) {
        if (!labels || !label_token(token->text))
            return reader_error(assembler->reader, token->line,
                                "invalid %s '%s'", address ? "target" : "16-bit value",
                                token->text);
        if (!add_label(assembler, token, true))
            return false;
        *value = 0;
    }
    next_token(assembler);
    return true;
}

static bool emit(Assembler *assembler, Opcode opcode, unsigned rd, unsigned ra,
                 unsigned rb, uint16_t payload)
{
    Image *image = assembler->image;
    void *storage = image->code;
    if (image->n == WORD_SPACE)
        return reader_error(assembler->reader, assembler->token.line,
                            "expanded code exceeds 65536 instructions");
    if (!grow_array(&storage, &image->code_capacity, (size_t)image->n + 1,
                    sizeof *image->code, WORD_SPACE)) {
        assembler->resource_error = true;
        return reader_error(assembler->reader, assembler->token.line,
                            "cannot allocate instruction storage");
    }
    image->code = storage;
    image->code[image->n++] = encode_instruction((Instruction){opcode, rd, ra, rb, payload});
    return true;
}

static int mnemonic(const char *text)
{
    static const char *const pseudo[] = {"MOV", "SUB", "INC", "DEC"};
    for (int i = 0; i < 16; ++i)
        if (i != OP_RESERVED && equal_keyword(text, opcode_names[i]))
            return i;
    for (int i = 0; i < 4; ++i)
        if (equal_keyword(text, pseudo[i]))
            return 16 + i;
    return -1;
}

static bool assemble_one(Assembler *assembler, const Token *name)
{
    int op = mnemonic(name->text);
    unsigned rd = 0, ra = 0, rb = 0;
    uint16_t value = 0;
    switch (op) {
    case OP_ADD: case OP_AND: case OP_EQ: case OP_OR: case OP_LT: case PSEUDO_SUB:
        if (!take_reg(assembler, &rd) || !take(assembler, ",") ||
            !take_reg(assembler, &ra) || !take(assembler, ",") || !take_reg(assembler, &rb))
            return false;
        if (op == PSEUDO_SUB)
            return emit(assembler, OP_NOT, 12, rb, 0, 0) &&
                   emit(assembler, OP_ADDI, 12, 12, 0, 1) &&
                   emit(assembler, OP_ADD, rd, ra, 12, 0);
        break;
    case OP_ADDI:
        if (!take_reg(assembler, &rd) || !take(assembler, ",") ||
            !take_reg(assembler, &ra) || !take(assembler, ",") ||
            !take_value(assembler, &value, false, false))
            return false;
        break;
    case OP_MOVI:
        if (!take_reg(assembler, &rd) || !take(assembler, ",") ||
            !take_value(assembler, &value, false, true))
            return false;
        break;
    case OP_NOT: case PSEUDO_MOV:
        if (!take_reg(assembler, &rd) || !take(assembler, ",") || !take_reg(assembler, &ra))
            return false;
        if (op == PSEUDO_MOV)
            op = OP_ADDI;
        break;
    case OP_LOAD:
        if (!take_reg(assembler, &rd) || !take(assembler, ",") || !take(assembler, "[") ||
            !take_reg(assembler, &ra) || !take(assembler, "]"))
            return false;
        break;
    case OP_STORE:
        if (!take(assembler, "[") || !take_reg(assembler, &ra) || !take(assembler, "]") ||
            !take(assembler, ",") || !take_reg(assembler, &rb))
            return false;
        break;
    case OP_JMP:
        if (assembler->token_status == 1 && reg_token(assembler->token.text, &ra)) {
            rb = 1;
            next_token(assembler);
        } else if (!take_value(assembler, &value, true, true)) {
            return false;
        }
        break;
    case OP_JZ:
        if (!take_reg(assembler, &ra) || !take(assembler, ",") ||
            !take_value(assembler, &value, true, true))
            return false;
        break;
    case OP_GUESS:
        if (!take_reg(assembler, &rd))
            return false;
        break;
    case OP_ASSERT:
        if (!take_reg(assembler, &ra))
            return false;
        break;
    case PSEUDO_INC: case PSEUDO_DEC:
        if (!take_reg(assembler, &rd))
            return false;
        ra = rd;
        value = op == PSEUDO_INC ? 1 : UINT16_MAX;
        op = OP_ADDI;
        break;
    case OP_STOP:
        break;
    default:
        return reader_error(assembler->reader, name->line, "unknown instruction '%s'", name->text);
    }
    return emit(assembler, (Opcode)op, rd, ra, rb, value);
}

static int compare_labels(const void *a, const void *b)
{
    return strcmp(((const Label *)a)->name, ((const Label *)b)->name);
}

static bool resolve_labels(Assembler *assembler)
{
    Image *image = assembler->image;
    if (assembler->label_count > 1)
        qsort(assembler->labels, assembler->label_count, sizeof(Label), compare_labels);
    for (size_t i = 1; i < assembler->label_count; ++i) {
        Label *label = &assembler->labels[i];
        if (strcmp(label->name, assembler->labels[i - 1].name) == 0)
            return reader_error(assembler->reader, label->line, "duplicate label '%s'", label->name);
    }
    for (size_t i = 0; i < assembler->fixup_count; ++i) {
        Label *fixup = &assembler->fixups[i];
        const Label *label = assembler->label_count ?
            bsearch(fixup, assembler->labels, assembler->label_count, sizeof(Label), compare_labels) :
            NULL;
        if (!label || label->pc >= image->n)
            return reader_error(assembler->reader, fixup->line,
                                "label '%s' does not name an instruction", fixup->name);
        image->code[fixup->pc] |= label->pc;
    }
    for (uint32_t pc = 0; pc < image->n; ++pc) {
        Instruction instruction;
        if (!decode_instruction(image->code[pc], &instruction))
            return reader_error(assembler->reader, assembler->reader->line,
                                "invalid encoding at PC %u", (unsigned)pc);
        if ((instruction.opcode == OP_JZ || (instruction.opcode == OP_JMP && !instruction.rb)) &&
            instruction.payload >= image->n)
            return reader_error(assembler->reader, assembler->reader->line,
                                "absolute target outside code at PC %u", (unsigned)pc);
    }
    return true;
}

static bool assemble(Assembler *assembler)
{
    next_token(assembler);
    while (assembler->token_status == 1) {
        Token name = assembler->token;
        next_token(assembler);
        if (assembler->token_status == 1 && strcmp(assembler->token.text, ":") == 0) {
            if (!label_token(name.text))
                return reader_error(assembler->reader, name.line, "invalid label '%s'", name.text);
            if (!add_label(assembler, &name, false))
                return false;
            next_token(assembler);
        } else if (!assemble_one(assembler, &name)) {
            return false;
        }
    }
    if (assembler->token_status < 0)
        return false;
    if (!assembler->image->n)
        return reader_error(assembler->reader, assembler->reader->line, "empty code");
    return resolve_labels(assembler);
}

static bool read_metadata(Assembler *assembler)
{
    Reader *reader = assembler->reader;
    Image *image = assembler->image;
    Token token;
    uint64_t size;
    if (!reader_expect(reader, "BNDETC-IMAGE") || !reader_expect(reader, BNDETC_FORMAT) ||
        !reader_expect(reader, "ISA") || !reader_expect(reader, BNDETC_FORMAT) ||
        !reader_expect(reader, "INPUT") || !reader_uint(reader, WORD_SPACE, &size))
        return false;
    image->i = (uint32_t)size;
    if (image->i && !(image->input = calloc(image->i, sizeof *image->input))) {
        assembler->resource_error = true;
        return reader_error(reader, reader->line, "cannot allocate input");
    }
    for (uint32_t i = 0; i < image->i; ++i) {
        if (!reader_need(reader, &token))
            return false;
        if (!parse_word(token.text, &image->input[i]))
            return reader_error(reader, token.line, "invalid input word '%s'", token.text);
    }
    if (!reader_expect(reader, "PRIVATE") || !reader_uint(reader, WORD_SPACE - image->i, &size))
        return false;
    image->m = (uint32_t)size;
    if (image->m && !(image->export_private = calloc(image->m, 1))) {
        assembler->resource_error = true;
        return reader_error(reader, reader->line, "cannot allocate export descriptor");
    }
    while (reader_need(reader, &token)) {
        if (equal_keyword(token.text, "CODE"))
            return true;
        if (!equal_keyword(token.text, "EXPORT"))
            return reader_error(reader, token.line, "expected EXPORT or CODE");
        if (!reader_need(reader, &token))
            return false;
        if (equal_keyword(token.text, "REG")) {
            unsigned reg;
            if (!reader_need(reader, &token))
                return false;
            if (!reg_token(token.text, &reg))
                return reader_error(reader, token.line, "invalid export register");
            image->export_registers |= (uint16_t)(1u << reg);
        } else if (equal_keyword(token.text, "MEM")) {
            uint64_t address, count;
            if (!reader_uint(reader, UINT16_MAX, &address) ||
                !reader_uint(reader, WORD_SPACE, &count))
                return false;
            if (!count || address < image->i || address + count > image->i + image->m)
                return reader_error(reader, token.line, "export range is outside private memory");
            memset(image->export_private + (size_t)(address - image->i), 1, (size_t)count);
        } else {
            return reader_error(reader, token.line, "expected REG or MEM");
        }
    }
    return false;
}

static uint64_t hash_value(uint64_t hash, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        hash ^= (value >> (i * 8)) & 255u;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t fingerprint(const Image *image)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_value(hash, image->n);
    hash = hash_value(hash, image->i);
    hash = hash_value(hash, image->m);
    hash = hash_value(hash, image->export_registers);
    for (uint32_t i = 0; i < image->n; ++i)
        hash = hash_value(hash, image->code[i]);
    for (uint32_t i = 0; i < image->i; ++i)
        hash = hash_value(hash, image->input[i]);
    for (uint32_t i = 0; i < image->m; ++i)
        hash = hash_value(hash, image->export_private[i]);
    return hash;
}

bool image_load(Image *image, const char *path, char error[256], bool *resource_error)
{
    Reader reader;
    Assembler assembler = {.reader = &reader, .image = image};
    bool ok;
    *image = (Image){0};
    ok = reader_open(&reader, path, false) && read_metadata(&assembler) && assemble(&assembler);
    *resource_error = assembler.resource_error;
    snprintf(error, 256, "%s", reader.error);
    reader_close(&reader);
    free(assembler.labels);
    free(assembler.fixups);
    if (ok)
        image->fingerprint = fingerprint(image);
    else
        image_free(image);
    return ok;
}

void image_free(Image *image)
{
    free(image->code);
    free(image->input);
    free(image->export_private);
    *image = (Image){0};
}
