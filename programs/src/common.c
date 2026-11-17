#define _XOPEN_SOURCE 700
#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

bool reader_error(Reader *reader, size_t line, const char *format, ...)
{
    if (!reader->error[0]) {
        char detail[160];
        va_list args;
        va_start(args, format);
        vsnprintf(detail, sizeof detail, format, args);
        va_end(args);
        snprintf(reader->error, sizeof reader->error, "line %zu: %s", line, detail);
    }
    return false;
}

bool reader_open(Reader *reader, const char *path, bool dimacs)
{
    *reader = (Reader){.path = path, .line = 1, .at_line_start = true,
                       .dimacs = dimacs};
    reader->file = fopen(path, "rb");
    if (!reader->file)
        return reader_error(reader, 1, "cannot open: %s", strerror(errno));
    reader->ch = fgetc(reader->file);
    if (reader->ch == 0xef) {
        int second = fgetc(reader->file);
        int third = fgetc(reader->file);
        if (second != 0xbb || third != 0xbf)
            return reader_error(reader, 1, "invalid UTF-8 prefix");
        reader->ch = fgetc(reader->file);
    }
    return true;
}

void reader_close(Reader *reader)
{
    if (reader->file) {
        fclose(reader->file);
        reader->file = NULL;
    }
}

static void advance(Reader *reader)
{
    if (reader->ch == '\n') {
        if (reader->line < SIZE_MAX)
            ++reader->line;
        reader->at_line_start = true;
    } else if (reader->ch != EOF && !isspace(reader->ch)) {
        reader->at_line_start = false;
    }
    reader->ch = fgetc(reader->file);
}

static bool comment(const Reader *reader)
{
    if (reader->dimacs)
        return reader->at_line_start && reader->ch == 'c';
    return reader->ch == '#' || reader->ch == ';';
}

static bool punctuation(const Reader *reader)
{
    return !reader->dimacs && reader->ch != EOF && reader->ch != 0 &&
           strchr(",[]:", reader->ch) != NULL;
}

int reader_next(Reader *reader, Token *token)
{
    size_t length = 0;
    if (reader->error[0])
        return -1;
    while (reader->ch != EOF) {
        if (comment(reader)) {
            while (reader->ch != EOF && reader->ch != '\n')
                advance(reader);
        } else if (isspace(reader->ch)) {
            advance(reader);
        } else {
            break;
        }
    }
    token->line = reader->line;
    if (reader->ch == EOF) {
        if (ferror(reader->file)) {
            reader_error(reader, token->line, "input read failed");
            return -1;
        }
        return 0;
    }
    if (punctuation(reader)) {
        token->text[0] = (char)reader->ch;
        token->text[1] = '\0';
        advance(reader);
        return 1;
    }
    while (reader->ch != EOF && !isspace(reader->ch) &&
           !punctuation(reader) && !comment(reader)) {
        if (length == sizeof token->text - 1 || reader->ch < 33 || reader->ch > 126) {
            reader_error(reader, token->line, "invalid or overlong token");
            return -1;
        }
        token->text[length++] = (char)reader->ch;
        advance(reader);
    }
    token->text[length] = '\0';
    return 1;
}

bool reader_need(Reader *reader, Token *token)
{
    int rc = reader_next(reader, token);
    if (rc == 0)
        return reader_error(reader, reader->line, "unexpected end of file");
    return rc == 1;
}

bool equal_keyword(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

bool reader_expect(Reader *reader, const char *text)
{
    Token token;
    if (!reader_need(reader, &token))
        return false;
    if (!equal_keyword(token.text, text))
        return reader_error(reader, token.line, "expected '%s', got '%s'", text, token.text);
    return true;
}

bool reader_end(Reader *reader)
{
    Token token;
    int rc = reader_next(reader, &token);
    if (rc == 1)
        return reader_error(reader, token.line, "unexpected token '%s'", token.text);
    return rc == 0;
}

static bool parse_natural(const char *text, int base, uint64_t maximum, uint64_t *value)
{
    char *end;
    uintmax_t number;
    if (!*text || *text == '-' || *text == '+' || isspace((unsigned char)*text))
        return false;
    errno = 0;
    number = strtoumax(text, &end, base);
    if (errno || end == text || *end || number > maximum)
        return false;
    *value = (uint64_t)number;
    return true;
}

bool parse_uint(const char *text, uint64_t maximum, uint64_t *value)
{
    return parse_natural(text, 10, maximum, value);
}

bool reader_uint(Reader *reader, uint64_t maximum, uint64_t *value)
{
    Token token;
    if (!reader_need(reader, &token))
        return false;
    if (!parse_uint(token.text, maximum, value))
        return reader_error(reader, token.line, "integer out of range: '%s'", token.text);
    return true;
}

static bool hex_prefix(const char *text)
{
    return text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
}

bool parse_word(const char *text, uint16_t *value)
{
    uint64_t raw;
    char *end;
    intmax_t number;
    if (hex_prefix(text)) {
        if (!parse_natural(text + 2, 16, UINT16_MAX, &raw))
            return false;
        *value = (uint16_t)raw;
        return true;
    }
    errno = 0;
    number = strtoimax(text, &end, 10);
    if (errno || end == text || *end || number < -32768 || number > 32767)
        return false;
    *value = (uint16_t)number;
    return true;
}

bool parse_address(const char *text, uint16_t *value)
{
    uint64_t raw;
    bool hex = hex_prefix(text);
    if (!parse_natural(hex ? text + 2 : text, hex ? 16 : 10, UINT16_MAX, &raw))
        return false;
    *value = (uint16_t)raw;
    return true;
}

bool size_add(size_t a, size_t b, size_t *result)
{
    if (a > SIZE_MAX - b)
        return false;
    *result = a + b;
    return true;
}

bool size_mul(size_t a, size_t b, size_t *result)
{
    if (b && a > SIZE_MAX / b)
        return false;
    *result = a * b;
    return true;
}

bool grow_array(void **data, size_t *capacity, size_t needed,
                size_t element_size, size_t maximum)
{
    size_t next;
    void *replacement;
    if (needed <= *capacity)
        return true;
    if (maximum > SIZE_MAX / element_size)
        maximum = SIZE_MAX / element_size;
    if (needed > maximum)
        return false;
    next = *capacity ? *capacity : 16;
    if (next > maximum)
        next = maximum;
    while (next < needed)
        next = next > maximum / 2 ? maximum : next * 2;
    replacement = realloc(*data, next * element_size);
    if (!replacement)
        return false;
    *data = replacement;
    *capacity = next;
    return true;
}

#ifdef _WIN32
static bool same_file(const char *a, const char *b)
{
    char *full_a = _fullpath(NULL, a, 0);
    char *full_b = _fullpath(NULL, b, 0);
    bool same = full_a && full_b && _stricmp(full_a, full_b) == 0;
    HANDLE first, second;
    BY_HANDLE_FILE_INFORMATION x, y;
    free(full_a);
    free(full_b);
    if (same)
        return true;
    first = CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    second = CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(first, &x) && GetFileInformationByHandle(second, &y)) {
        same = x.dwVolumeSerialNumber == y.dwVolumeSerialNumber &&
               x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
    }
    if (first != INVALID_HANDLE_VALUE)
        CloseHandle(first);
    if (second != INVALID_HANDLE_VALUE)
        CloseHandle(second);
    return same;
}
#else
static char *parent_directory(const char *path)
{
    const char *slash = strrchr(path, '/');
    char *parent, *resolved;
    size_t length;
    if (!slash)
        return realpath(".", NULL);
    length = slash == path ? 1 : (size_t)(slash - path);
    parent = malloc(length + 1);
    if (!parent)
        return NULL;
    memcpy(parent, path, length);
    parent[length] = '\0';
    resolved = realpath(parent, NULL);
    free(parent);
    return resolved;
}

static bool same_file(const char *a, const char *b)
{
    struct stat x, y;
    const char *name_a, *name_b;
    char *parent_a, *parent_b;
    bool same;
    if (stat(a, &x) == 0 && stat(b, &y) == 0 &&
        x.st_dev == y.st_dev && x.st_ino == y.st_ino)
        return true;
    name_a = strrchr(a, '/');
    name_b = strrchr(b, '/');
    if (strcmp(name_a ? name_a + 1 : a, name_b ? name_b + 1 : b) != 0)
        return false;
    parent_a = parent_directory(a);
    parent_b = parent_directory(b);
    same = parent_a && parent_b && strcmp(parent_a, parent_b) == 0;
    free(parent_a);
    free(parent_b);
    return same;
}
#endif

bool paths_distinct(const char *const *paths, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!paths[i])
            continue;
        for (size_t j = 0; j < i; ++j) {
            if (paths[j] && (strcmp(paths[i], paths[j]) == 0 || same_file(paths[i], paths[j]))) {
                fprintf(stderr, "Paths must be distinct: %s and %s\n", paths[j], paths[i]);
                return false;
            }
        }
    }
    return true;
}

static FILE *create_exclusive(const char *path)
{
#ifdef _WIN32
    int fd = _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    FILE *file;
    if (fd < 0)
        return NULL;
    file = _fdopen(fd, "wb");
    if (!file) {
        int saved = errno;
        _close(fd);
        remove(path);
        errno = saved;
    }
    return file;
#else
    return fopen(path, "wbx");
#endif
}

bool output_open(Output *output, const char *path)
{
    size_t length;
    char *temporary;
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    *output = (Output){.path = path};
    if (!size_add(strlen(path), 64, &length) || !(temporary = malloc(length))) {
        fprintf(stderr, "%s: cannot allocate output path\n", path);
        return false;
    }
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        snprintf(temporary, length, "%s.tmp.%lu.%u", path, pid, attempt);
        output->file = create_exclusive(temporary);
        if (output->file) {
            output->temporary = temporary;
            return true;
        }
        if (errno != EEXIST)
            break;
    }
    fprintf(stderr, "%s: cannot open staged output: %s\n", path, strerror(errno));
    free(temporary);
    return false;
}

bool output_seal(Output *output)
{
    bool ok;
    if (!output->file)
        return output->temporary != NULL;
    ok = !ferror(output->file);
    if (fclose(output->file) != 0)
        ok = false;
    output->file = NULL;
    if (!ok)
        fprintf(stderr, "%s: output write failed\n", output->path);
    return ok;
}

bool output_publish(Output *output)
{
    if (!output_seal(output))
        return false;
#ifdef _WIN32
    if (!MoveFileExA(output->temporary, output->path, MOVEFILE_REPLACE_EXISTING)) {
        fprintf(stderr, "%s: cannot publish output (Windows error %lu)\n",
                output->path, (unsigned long)GetLastError());
        return false;
    }
#else
    if (rename(output->temporary, output->path) != 0) {
        fprintf(stderr, "%s: cannot publish output: %s\n", output->path, strerror(errno));
        return false;
    }
#endif
    free(output->temporary);
    output->temporary = NULL;
    return true;
}

void output_discard(Output *output)
{
    if (output->file)
        fclose(output->file);
    if (output->temporary) {
        remove(output->temporary);
        free(output->temporary);
    }
    *output = (Output){0};
}

void csv_string(FILE *file, const char *text)
{
    fputc('"', file);
    for (; *text; ++text) {
        if (*text == '"')
            fputc('"', file);
        fputc((unsigned char)*text, file);
    }
    fputc('"', file);
}

#ifdef _WIN32
static volatile LONG stop_flag;
static BOOL WINAPI handle_console(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        InterlockedExchange(&stop_flag, 1);
        return TRUE;
    }
    return FALSE;
}

bool install_stop_handler(void)
{
    return SetConsoleCtrlHandler(handle_console, TRUE) != 0;
}

bool interrupted(void)
{
    return InterlockedCompareExchange(&stop_flag, 0, 0) != 0;
}

uint64_t monotonic_ms(void)
{
    return GetTickCount64();
}
#else
static volatile sig_atomic_t stop_flag;
static void handle_signal(int signum)
{
    (void)signum;
    stop_flag = 1;
}

bool install_stop_handler(void)
{
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGINT, &action, NULL) == 0 &&
           sigaction(SIGTERM, &action, NULL) == 0;
}

bool interrupted(void)
{
    return stop_flag != 0;
}

uint64_t monotonic_ms(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return 0;
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}
#endif

void print_version(const char *program)
{
    printf("%s %s (BNdetC ISA and text format %s)\n",
           program, BNDETC_VERSION, BNDETC_FORMAT);
}

const char *outcome_name(Outcome outcome)
{
    static const char *const names[] = {"RUNNING", "ACCEPT", "EXHAUSTED", "UNKNOWN", "ERROR"};
    return names[outcome];
}

int outcome_exit_code(Outcome outcome)
{
    if (outcome == OUT_ACCEPT || outcome == OUT_EXHAUSTED)
        return 0;
    return outcome == OUT_UNKNOWN ? 2 : 1;
}
