#include "dimacs.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

bool dimacs_open(Dimacs *cnf, const char *path)
{
    uint64_t variables;
    *cnf = (Dimacs){0};
    if (!reader_open(&cnf->reader, path, true) ||
        !reader_expect(&cnf->reader, "p") ||
        !reader_expect(&cnf->reader, "cnf") ||
        !reader_uint(&cnf->reader, INT32_MAX, &variables) ||
        !reader_uint(&cnf->reader, UINT64_MAX, &cnf->clauses))
        return false;
    cnf->variables = (uint32_t)variables;
    return true;
}

int dimacs_next(Dimacs *cnf, int32_t *literal)
{
    Token token;
    char *end;
    intmax_t number;
    int rc = reader_next(&cnf->reader, &token);
    if (rc < 0)
        return -1;
    if (rc == 0) {
        if (!cnf->open_clause && cnf->closed == cnf->clauses)
            return 0;
        reader_error(&cnf->reader, cnf->reader.line,
                     "clause count mismatch or missing terminating zero");
        return -1;
    }
    if (cnf->closed == cnf->clauses) {
        reader_error(&cnf->reader, token.line, "more clauses than declared");
        return -1;
    }
    errno = 0;
    number = strtoimax(token.text, &end, 10);
    if (errno || end == token.text || *end ||
        number < -(intmax_t)cnf->variables || number > (intmax_t)cnf->variables) {
        reader_error(&cnf->reader, token.line, "invalid literal '%s'", token.text);
        return -1;
    }
    *literal = (int32_t)number;
    cnf->open_clause = number != 0;
    if (number == 0)
        ++cnf->closed;
    return 1;
}
