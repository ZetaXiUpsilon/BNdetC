# GNU Make with GCC or a compatible C17 compiler.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Wpedantic
OUT ?= build/native

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
EXEEXT ?= .exe
CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
else
EXEEXT ?=
endif

COMMON := src/common.c
DIMACS := src/dimacs.c
EXECUTOR := src/ndexec.c src/isa.c src/image.c src/runtime.c src/functional.c src/timed.c src/report.c
HEADERS := $(wildcard src/*.h)
TOOLS := $(addprefix $(OUT)/,cnf2isa$(EXEEXT) ndexec$(EXEEXT) satref$(EXEEXT))

.PHONY: all
all: $(TOOLS)

$(OUT):
ifeq ($(OS),Windows_NT)
	@if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
else
	mkdir -p "$@"
endif

$(OUT)/cnf2isa$(EXEEXT): $(COMMON) $(DIMACS) src/cnf2isa.c $(HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o "$@"

$(OUT)/ndexec$(EXEEXT): $(COMMON) $(EXECUTOR) $(HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o "$@"

$(OUT)/satref$(EXEEXT): $(COMMON) $(DIMACS) src/satref.c $(HEADERS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) $(LDFLAGS) -o "$@"
