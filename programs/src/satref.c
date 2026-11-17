#include "dimacs.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t variables;
    int32_t *tokens;
    size_t length;
} Formula;

static Outcome load_formula(Formula *formula, const char *path)
{
    Dimacs cnf;
    int32_t literal;
    int rc;
    size_t capacity = 0;
    if (!dimacs_open(&cnf, path)) {
        fprintf(stderr, "%s: %s\n", path, cnf.reader.error);
        reader_close(&cnf.reader);
        return OUT_ERROR;
    }
    formula->variables = cnf.variables;
    while ((rc = dimacs_next(&cnf, &literal)) == 1) {
        void *data = formula->tokens;
        if (formula->length == SIZE_MAX ||
            !grow_array(&data, &capacity, formula->length + 1, sizeof *formula->tokens,
                        SIZE_MAX / sizeof *formula->tokens)) {
            reader_close(&cnf.reader);
            return OUT_UNKNOWN;
        }
        formula->tokens = data;
        formula->tokens[formula->length++] = literal;
    }
    if (rc < 0)
        fprintf(stderr, "%s: %s\n", path, cnf.reader.error);
    reader_close(&cnf.reader);
    return rc < 0 ? OUT_ERROR : OUT_RUNNING;
}

static bool satisfies(const Formula *formula, const unsigned char *assignment)
{
    bool clause = false;
    for (size_t i = 0; i < formula->length; ++i) {
        int32_t literal = formula->tokens[i];
        if (literal == 0) {
            if (!clause)
                return false;
            clause = false;
        } else {
            uint32_t index = (uint32_t)(literal < 0 ? -literal : literal) - 1;
            if ((assignment[index] != 0) == (literal > 0))
                clause = true;
        }
    }
    return true;
}

static Outcome enumerate(const Formula *formula, unsigned maximum,
                         unsigned char assignment[63], const char **reason)
{
    uint64_t limit;
    if (formula->variables > maximum) {
        *reason = "VARIABLE_LIMIT";
        return OUT_UNKNOWN;
    }
    limit = UINT64_C(1) << formula->variables;
    for (uint64_t bits = 0; bits < limit; ++bits) {
        if (interrupted()) {
            *reason = "INTERRUPTED";
            return OUT_UNKNOWN;
        }
        for (uint32_t i = 0; i < formula->variables; ++i)
            assignment[i] = (unsigned char)((bits >> i) & 1u);
        if (satisfies(formula, assignment))
            return OUT_ACCEPT;
    }
    return OUT_EXHAUSTED;
}

static bool result_field(Reader *reader, const char *name, uint64_t maximum)
{
    Token token;
    uint64_t value;
    if (!reader_expect(reader, name) || !reader_need(reader, &token))
        return false;
    if (!equal_keyword(token.text, "NONE") && !parse_uint(token.text, maximum, &value))
        return reader_error(reader, token.line, "invalid %s", name);
    return true;
}

static bool parse_result(Reader *reader, Outcome *outcome, uint32_t **memory)
{
    Token token, value;
    uint16_t seen_registers = 0;
    if (!reader_expect(reader, "BNDETC-RESULT") || !reader_expect(reader, BNDETC_FORMAT) ||
        !reader_expect(reader, "STATUS") || !reader_need(reader, &token))
        return false;
    for (Outcome code = OUT_ACCEPT; code <= OUT_ERROR; ++code) {
        if (equal_keyword(token.text, outcome_name(code)))
            *outcome = code;
    }
    if (*outcome == OUT_RUNNING)
        return reader_error(reader, token.line, "invalid result status");
    if (!reader_expect(reader, "REASON") || !reader_need(reader, &token) ||
        !result_field(reader, "PC", UINT16_MAX) || !result_field(reader, "HANDLE", UINT64_MAX))
        return false;
    if (*outcome == OUT_ACCEPT && !(*memory = calloc(WORD_SPACE, sizeof **memory)))
        return reader_error(reader, reader->line, "cannot allocate witness buffer");
    while (reader_need(reader, &token)) {
        uint64_t index;
        uint16_t word;
        bool reg = equal_keyword(token.text, "REG");
        if (equal_keyword(token.text, "END"))
            return reader_end(reader);
        if (*outcome != OUT_ACCEPT || (!reg && !equal_keyword(token.text, "MEM")))
            return reader_error(reader, token.line, "unexpected result record");
        if (reg) {
            if (!reader_need(reader, &value))
                return false;
            if ((value.text[0] != 'R' && value.text[0] != 'r') ||
                !parse_uint(value.text + 1, 11, &index))
                return reader_error(reader, value.line, "invalid result register");
            if (seen_registers & (1u << index))
                return reader_error(reader, value.line, "duplicate result register");
            seen_registers |= (uint16_t)(1u << index);
        } else if (!reader_uint(reader, UINT16_MAX, &index)) {
            return false;
        }
        if (!reader_need(reader, &value))
            return false;
        if (!parse_word(value.text, &word))
            return reader_error(reader, value.line, "invalid result word");
        if (!reg) {
            if ((*memory)[index])
                return reader_error(reader, token.line, "duplicate result address");
            (*memory)[index] = (uint32_t)word + 1;
        }
    }
    return false;
}

static bool read_assignment(Reader *reader, const Formula *formula, uint32_t *memory,
                            unsigned char *assignment)
{
    uint64_t variables;
    if (!reader_expect(reader, "BNDETC-MAP") || !reader_expect(reader, BNDETC_FORMAT) ||
        !reader_expect(reader, "VARIABLES") || !reader_uint(reader, WORD_SPACE, &variables))
        return false;
    if (variables != formula->variables)
        return reader_error(reader, reader->line, "map variable count differs from CNF");
    for (uint32_t i = 0; i < formula->variables; ++i) {
        uint64_t variable, address;
        if (!reader_uint(reader, variables, &variable) ||
            !reader_uint(reader, UINT16_MAX, &address))
            return false;
        if (variable != (uint64_t)i + 1 || memory[address] < 1 || memory[address] > 2)
            return reader_error(reader, reader->line,
                                "invalid mapping or missing, non-Boolean or repeated witness word");
        assignment[i] = (unsigned char)(memory[address] - 1);
        memory[address] = 0;
    }
    return reader_expect(reader, "END") && reader_end(reader);
}

static Outcome verify_witness(const Formula *formula, const char *map_path, uint32_t *memory)
{
    Reader reader;
    unsigned char *assignment = NULL;
    Outcome outcome = OUT_ERROR;
    if (formula->variables > WORD_SPACE) {
        fprintf(stderr, "CNF has more variables than addressable witness words\n");
        return OUT_ERROR;
    }
    if (formula->variables && !(assignment = malloc(formula->variables)))
        return OUT_UNKNOWN;
    if (!reader_open(&reader, map_path, false) ||
        !read_assignment(&reader, formula, memory, assignment)) {
        fprintf(stderr, "%s: %s\n", map_path, reader.error);
    } else if (satisfies(formula, assignment)) {
        puts("VERIFIED SAT");
        outcome = OUT_ACCEPT;
    } else {
        fprintf(stderr, "Exported assignment does not satisfy the original CNF\n");
    }
    reader_close(&reader);
    free(assignment);
    return outcome;
}

static Outcome check_result(const Formula *formula, const char *result_path,
                            const char *map_path, unsigned maximum, const char **reason)
{
    Reader reader;
    Outcome claimed = OUT_RUNNING, outcome = OUT_ERROR;
    uint32_t *memory = NULL;
    unsigned char assignment[63];
    if (!reader_open(&reader, result_path, false) || !parse_result(&reader, &claimed, &memory)) {
        fprintf(stderr, "%s: %s\n", result_path, reader.error);
    } else if (claimed == OUT_ACCEPT) {
        outcome = verify_witness(formula, map_path, memory);
    } else if (claimed == OUT_EXHAUSTED) {
        outcome = enumerate(formula, maximum, assignment, reason);
        if (outcome == OUT_EXHAUSTED)
            puts("VERIFIED UNSAT");
        else if (outcome == OUT_ACCEPT) {
            fprintf(stderr, "Counterexample: original CNF is satisfiable\n");
            outcome = OUT_ERROR;
        }
    } else {
        outcome = claimed;
        *reason = claimed == OUT_UNKNOWN ? "EXECUTOR_UNKNOWN" : "EXECUTOR_ERROR";
    }
    reader_close(&reader);
    free(memory);
    return outcome;
}

static void usage(void)
{
    puts("Usage: satref input.cnf [--check result.txt program.map] [--max-vars N]");
    puts("Default enumeration limit: 20 variables. Witness checking is independent of this limit.");
}

int main(int argc, char **argv)
{
    Formula formula = {0};
    const char *result = NULL, *map = NULL, *reason = "ALLOCATION";
    uint64_t maximum = 20;
    Outcome outcome;
    unsigned char assignment[63];
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        print_version("satref");
        return 0;
    }
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        usage();
        return argc < 2;
    }
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0 && !result && i + 2 < argc) {
            result = argv[++i];
            map = argv[++i];
        } else if (strcmp(argv[i], "--max-vars") == 0 && i + 1 < argc &&
                   parse_uint(argv[i + 1], 63, &maximum)) {
            ++i;
        } else {
            usage();
            return 1;
        }
    }
    if (!install_stop_handler()) {
        fprintf(stderr, "Cannot install cancellation handler\n");
        return 1;
    }
    outcome = load_formula(&formula, argv[1]);
    if (outcome == OUT_RUNNING && result) {
        outcome = check_result(&formula, result, map, (unsigned)maximum, &reason);
    } else if (outcome == OUT_RUNNING) {
        outcome = enumerate(&formula, (unsigned)maximum, assignment, &reason);
        if (outcome == OUT_ACCEPT) {
            puts("SAT");
            fputs("v", stdout);
            for (uint32_t i = 0; i < formula.variables; ++i)
                printf(" %s%u", assignment[i] ? "" : "-", (unsigned)i + 1);
            puts(" 0");
        } else if (outcome == OUT_EXHAUSTED) {
            puts("UNSAT");
        }
    }
    if (outcome == OUT_UNKNOWN || outcome == OUT_ERROR)
        printf("%s (%s)\n", outcome_name(outcome), outcome == OUT_ERROR ? "ERROR" : reason);
    free(formula.tokens);
    return outcome_exit_code(outcome);
}
