#include "dimacs.h"

#include <stdarg.h>
#include <string.h>

static bool emit(FILE *file, uint32_t *count, const char *format, ...)
{
    va_list args;
    int rc;
    if (*count == WORD_SPACE) {
        fprintf(stderr, "Expanded code exceeds 65536 instructions\n");
        return false;
    }
    va_start(args, format);
    rc = vfprintf(file, format, args);
    va_end(args);
    if (rc < 0 || fputc('\n', file) == EOF)
        return false;
    ++*count;
    return true;
}

static bool translate(Dimacs *cnf, FILE *image, FILE *map, uint32_t *instructions)
{
    int rc;
    int32_t literal;
    bool new_clause = true;
    if (cnf->variables > (WORD_SPACE - 2) / 3) {
        fprintf(stderr, "Variable initialization alone exceeds code capacity\n");
        return false;
    }
    fprintf(image, "BNDETC-IMAGE %s\nISA %s\nINPUT 0\nPRIVATE %u\n",
            BNDETC_FORMAT, BNDETC_FORMAT, (unsigned)cnf->variables);
    if (cnf->variables)
        fprintf(image, "EXPORT MEM 0 %u\n", (unsigned)cnf->variables);
    fputs("CODE\n", image);
    fprintf(map, "BNDETC-MAP %s\nVARIABLES %u\n",
            BNDETC_FORMAT, (unsigned)cnf->variables);
    for (uint32_t i = 0; i < cnf->variables; ++i) {
        fprintf(map, "%u %u\n", (unsigned)i + 1, (unsigned)i);
        if (!emit(image, instructions, "MOVI R1, 0x%04X", (unsigned)i) ||
            !emit(image, instructions, "GUESS R0") ||
            !emit(image, instructions, "STORE [R1], R0"))
            return false;
    }
    fputs("END\n", map);
    if (!emit(image, instructions, "MOVI R3, 0"))
        return false;
    while ((rc = dimacs_next(cnf, &literal)) == 1) {
        if (new_clause && !emit(image, instructions, "MOVI R2, 0"))
            return false;
        if (literal == 0) {
            if (!emit(image, instructions, "ASSERT R2"))
                return false;
            new_clause = true;
        } else {
            unsigned address = (unsigned)(literal < 0 ? -literal : literal) - 1;
            if (!emit(image, instructions, "MOVI R1, 0x%04X", address) ||
                !emit(image, instructions, "LOAD R0, [R1]") ||
                (literal < 0 && !emit(image, instructions, "EQ R0, R0, R3")) ||
                !emit(image, instructions, "OR R2, R2, R0"))
                return false;
            new_clause = false;
        }
    }
    return rc == 0 && emit(image, instructions, "STOP") && !ferror(map);
}

int main(int argc, char **argv)
{
    Dimacs cnf = {0};
    Output image = {0}, map = {0};
    uint32_t instructions = 0;
    bool ok;
    const char *paths[3];
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        print_version("cnf2isa");
        return 0;
    }
    if (argc < 2 || argc > 4 || strcmp(argv[1], "--help") == 0) {
        puts("Usage: cnf2isa input.cnf [program.ndp [program.map]]");
        return argc == 2 && strcmp(argv[1], "--help") == 0 ? 0 : 1;
    }
    paths[0] = argv[1];
    paths[1] = argc > 2 ? argv[2] : "program.ndp";
    paths[2] = argc > 3 ? argv[3] : "program.map";
    if (!paths_distinct(paths, 3))
        return 1;
    ok = dimacs_open(&cnf, paths[0]) &&
         output_open(&image, paths[1]) && output_open(&map, paths[2]) &&
         translate(&cnf, image.file, map.file, &instructions);
    reader_close(&cnf.reader);
    if (ok)
        ok = output_seal(&image) && output_seal(&map) &&
             output_publish(&map) && output_publish(&image);
    if (!ok) {
        if (cnf.reader.error[0])
            fprintf(stderr, "%s: %s\n", paths[0], cnf.reader.error);
        fprintf(stderr, "Translation failed\n");
    } else {
        printf("Wrote %s and %s (%u native instructions)\n",
               paths[1], paths[2], (unsigned)instructions);
    }
    output_discard(&image);
    output_discard(&map);
    return ok ? 0 : 1;
}
