#ifndef BNDETC_COMMON_H
#define BNDETC_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BNDETC_VERSION "0.1.0"
#define BNDETC_FORMAT "0.1"
#define WORD_SPACE 65536u

typedef enum { OUT_RUNNING, OUT_ACCEPT, OUT_EXHAUSTED, OUT_UNKNOWN, OUT_ERROR } Outcome;
const char *outcome_name(Outcome outcome);
int outcome_exit_code(Outcome outcome);

typedef struct {
    char text[128];
    size_t line;
} Token;

typedef struct {
    FILE *file;
    const char *path;
    size_t line;
    int ch;
    bool at_line_start;
    bool dimacs;
    char error[256];
} Reader;

bool reader_open(Reader *reader, const char *path, bool dimacs);
void reader_close(Reader *reader);
int reader_next(Reader *reader, Token *token);
bool reader_need(Reader *reader, Token *token);
bool reader_expect(Reader *reader, const char *text);
bool reader_end(Reader *reader);
bool reader_error(Reader *reader, size_t line, const char *format, ...);
bool reader_uint(Reader *reader, uint64_t maximum, uint64_t *value);

bool parse_uint(const char *text, uint64_t maximum, uint64_t *value);
bool parse_word(const char *text, uint16_t *value);
bool parse_address(const char *text, uint16_t *value);
bool equal_keyword(const char *a, const char *b);
bool size_add(size_t a, size_t b, size_t *result);
bool size_mul(size_t a, size_t b, size_t *result);
bool grow_array(void **data, size_t *capacity, size_t needed,
                size_t element_size, size_t maximum);

typedef struct {
    FILE *file;
    const char *path;
    char *temporary;
} Output;

bool paths_distinct(const char *const *paths, size_t count);
bool output_open(Output *output, const char *path);
bool output_seal(Output *output);
bool output_publish(Output *output);
void output_discard(Output *output);
void csv_string(FILE *file, const char *text);

bool install_stop_handler(void);
bool interrupted(void);
uint64_t monotonic_ms(void);
void print_version(const char *program);

#endif
