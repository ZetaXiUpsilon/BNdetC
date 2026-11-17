#ifndef BNDETC_DIMACS_H
#define BNDETC_DIMACS_H

#include "common.h"

typedef struct {
    Reader reader;
    uint32_t variables;
    uint64_t clauses;
    uint64_t closed;
    bool open_clause;
} Dimacs;

bool dimacs_open(Dimacs *cnf, const char *path);
int dimacs_next(Dimacs *cnf, int32_t *literal);

#endif
