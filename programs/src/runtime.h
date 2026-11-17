#ifndef BNDETC_RUNTIME_H
#define BNDETC_RUNTIME_H

#include "isa.h"

#define NO_HANDLE SIZE_MAX
#define NO_PC UINT32_MAX

enum {
    FETCH_CYCLES = 1, ALU_CYCLES = 1, MEMORY_CYCLES = 2,
    COPY_CYCLES = 2, MANAGER_CYCLES = 1, EXPORT_REGISTER_CYCLES = 1
};

typedef struct {
    size_t slots, states;
    bool timed, timeout_set, cycle_limit_set;
    uint64_t timeout_ms, cycle_limit, trace_limit;
    const char *image_path, *result_path, *stats_path, *trace_path, *dump_path;
} RunOptions;

typedef enum { STATE_FREE, STATE_OWNED, STATE_READY, STATE_RESERVED, STATE_RETAINED } StateRole;

typedef struct {
    uint16_t pc;
    uint16_t registers[16];
    StateRole role;
} State;

typedef enum {
    ACTION_NEXT, ACTION_WRITE, ACTION_LOAD, ACTION_STORE, ACTION_GUESS,
    ACTION_FAIL, ACTION_STOP, ACTION_ERROR
} ActionKind;

typedef struct {
    ActionKind kind;
    Opcode opcode;
    unsigned rd;
    uint16_t pc, address, value;
    const char *reason;
} Action;

typedef enum {
    SLOT_EMPTY, SLOT_FETCH_READY, SLOT_FETCHING, SLOT_EXEC_READY, SLOT_EXECUTING,
    SLOT_MEMORY_READY, SLOT_MEMORY, SLOT_RESERVE, SLOT_COPY_READY, SLOT_COPYING,
    SLOT_FORK_READY, SLOT_FAILED, SLOT_PARKED
} SlotPhase;

typedef struct {
    size_t handle, destination;
    SlotPhase phase;
    uint64_t due;
    uint32_t instruction_word;
    Action action;
} Slot;

typedef struct {
    bool valid;
    uint64_t event, cycle;
} Stamp;

typedef struct {
    uint64_t events, issued[16], completed[16];
    uint64_t fetch_words, input_words, private_reads, private_writes;
    uint64_t copies, copy_read_bytes, copy_write_bytes, fork_patch_bytes;
    uint64_t claims, branch_failures, manager_transactions, export_words;
    uint64_t cancelled_entries, trace_rows;
    uint64_t alu_slot_cycles, owned_slot_cycles, fetch_wait_cycles;
    uint64_t input_wait_cycles, private_wait_cycles, copy_wait_cycles, manager_wait_cycles;
    uint64_t manager_busy_cycles, copy_busy_cycles, host_ms;
    size_t peak_states, peak_slots, peak_ready, peak_reserved, peak_retained;
    bool saturated, trace_truncated;
    Stamp first_success, winner_selected, result_ready, quiescent, reclaimed, terminal;
} Statistics;

typedef struct {
    const char *error;
    size_t error_handle, winner, capacity_handle;
    uint32_t error_pc, capacity_pc;
} Candidates;

typedef struct {
    bool is_register;
    unsigned index;
} ExportWord;

typedef struct {
    const Image *image;
    const RunOptions *options;
    State *pool;
    uint16_t *private_words;
    Slot *slots;
    size_t *ready;
    size_t used, ready_count, reserved, owned, reclaim_index;
    size_t logical_bytes, allocated_bytes;
    uint64_t cycle, host_start;
    Outcome outcome;
    const char *reason, *stop_request;
    size_t terminal_handle, winner;
    uint32_t terminal_pc;
    bool quiet, result_valid;
    Output result, trace;
    unsigned export_reg;
    uint32_t export_offset;
    Statistics stats;
} Machine;

bool machine_init(Machine *machine, const Image *image, const RunOptions *options);
void machine_destroy(Machine *machine);
void request_stop(Machine *machine, const char *reason);
void observe_stop(Machine *machine);
void increment(Machine *machine, uint64_t *counter, uint64_t amount);
void event(Machine *machine, const char *kind, size_t slot, size_t handle, uint32_t pc);
void runtime_fault(Machine *machine, const char *reason);

Candidates empty_candidates(void);
bool choose_outcome(Machine *machine, const Candidates *candidates, bool exhausted);
void set_outcome(Machine *machine, Outcome outcome, const char *reason,
                 size_t handle, uint32_t pc);

void finish_fetch(Machine *machine, size_t slot, bool ready);
void begin_instruction(Machine *machine, size_t slot);
void finish_instruction(Machine *machine, size_t slot, Candidates *candidates);
void finish_memory(Machine *machine, size_t slot, bool commit);
void reserve_fork(Machine *machine, size_t slot, size_t destination);
size_t free_handle(const Machine *machine);
void commit_fork(Machine *machine, size_t slot);
void assign_ready(Machine *machine, size_t slot);
void release_failed(Machine *machine, size_t slot);

uint16_t read_state_word(const Machine *machine, size_t handle, uint32_t word);
void write_state_word(Machine *machine, size_t handle, uint32_t word, uint16_t value);
void begin_drain(Machine *machine);
size_t next_reclaim(Machine *machine);
void reclaim_entry(Machine *machine, size_t handle);
bool next_export(Machine *machine, ExportWord *word);
void export_word(Machine *machine, ExportWord word);
void finish_export(Machine *machine);
void mark_reclaimed(Machine *machine);

void run_functional(Machine *machine);
size_t timing_control_bytes(void);
void run_timed(Machine *machine);
bool save_result(Machine *machine);
bool save_statistics(const Machine *machine);

#endif
