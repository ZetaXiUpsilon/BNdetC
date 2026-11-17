#include "runtime.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void peak(size_t *maximum, size_t value)
{
    if (value > *maximum)
        *maximum = value;
}

void increment(Machine *machine, uint64_t *counter, uint64_t amount)
{
    if (UINT64_MAX - *counter < amount) {
        *counter = UINT64_MAX;
        machine->stats.saturated = true;
    } else {
        *counter += amount;
    }
}

void event(Machine *machine, const char *kind, size_t slot, size_t handle, uint32_t pc)
{
    Statistics *stats = &machine->stats;
    FILE *trace = machine->trace.file;
    increment(machine, &stats->events, 1);
    if (!trace)
        return;
    if (stats->trace_rows >= machine->options->trace_limit) {
        stats->trace_truncated = true;
        return;
    }
    ++stats->trace_rows;
    fprintf(trace, "%" PRIu64 ",", stats->events);
    if (machine->options->timed)
        fprintf(trace, "%" PRIu64, machine->cycle);
    fprintf(trace, ",%s,", kind);
    if (slot != NO_HANDLE)
        fprintf(trace, "%zu", slot);
    fputc(',', trace);
    if (handle != NO_HANDLE)
        fprintf(trace, "%zu", handle);
    fputc(',', trace);
    if (pc != NO_PC)
        fprintf(trace, "%u", (unsigned)pc);
    fprintf(trace, ",%zu,%zu,%zu\n", machine->used, machine->ready_count, machine->reserved);
}

static Stamp stamp(const Machine *machine)
{
    return (Stamp){true, machine->stats.events, machine->cycle};
}

static void clear_slot(Slot *slot)
{
    *slot = (Slot){.handle = NO_HANDLE, .destination = NO_HANDLE, .phase = SLOT_EMPTY};
}

static bool memory_sizes(Machine *machine, size_t *private_count)
{
    const Image *image = machine->image;
    const RunOptions *options = machine->options;
    size_t state_bytes, words_bytes, slot_bytes, handle_bytes, pool_bytes;
    size_t runtime_bytes = sizeof *machine + sizeof *image + sizeof *options;
    size_t logical_pool, immutable, actual_image;
    if (!size_mul(options->states, image->m, private_count) ||
        !size_mul(*private_count, sizeof(uint16_t), &words_bytes) ||
        !size_mul(options->states, sizeof(State), &state_bytes) ||
        !size_mul(options->slots, sizeof(Slot), &slot_bytes) ||
        !size_mul(options->states, sizeof(size_t), &handle_bytes) ||
        !size_add(state_bytes, words_bytes, &pool_bytes) ||
        !size_add(runtime_bytes, slot_bytes, &runtime_bytes) ||
        !size_add(runtime_bytes, handle_bytes, &runtime_bytes) ||
        !size_add(runtime_bytes, image->m, &runtime_bytes) ||
        (options->timed && !size_add(runtime_bytes, timing_control_bytes(), &runtime_bytes)) ||
        !size_mul(options->states, 34u + 2u * image->m, &logical_pool))
        return false;
    immutable = 4u * image->n + 2u * image->i;
    actual_image = sizeof(uint32_t) * image->code_capacity + sizeof(uint16_t) * image->i;
    /* State carries ownership metadata and possibly host alignment padding. */
    if (!size_add(runtime_bytes, pool_bytes - logical_pool, &runtime_bytes) ||
        !size_add(logical_pool, immutable, &machine->logical_bytes) ||
        !size_add(machine->logical_bytes, runtime_bytes, &machine->logical_bytes) ||
        !size_add(runtime_bytes, logical_pool, &machine->allocated_bytes) ||
        !size_add(machine->allocated_bytes, actual_image, &machine->allocated_bytes))
        return false;
    return true;
}

bool machine_init(Machine *machine, const Image *image, const RunOptions *options)
{
    size_t private_count;
    *machine = (Machine){.image = image, .options = options, .winner = NO_HANDLE,
                        .terminal_handle = NO_HANDLE, .terminal_pc = NO_PC};
    if (!memory_sizes(machine, &private_count))
        return false;
    machine->pool = calloc(options->states, sizeof *machine->pool);
    machine->slots = calloc(options->slots, sizeof *machine->slots);
    machine->ready = calloc(options->states, sizeof *machine->ready);
    if (private_count)
        machine->private_words = calloc(private_count, sizeof *machine->private_words);
    if (!machine->pool || !machine->slots || !machine->ready ||
        (private_count && !machine->private_words))
        return false;
    for (size_t i = 0; i < options->slots; ++i)
        clear_slot(&machine->slots[i]);
    machine->pool[0].role = STATE_OWNED;
    machine->slots[0].handle = 0;
    machine->slots[0].phase = SLOT_FETCH_READY;
    machine->used = machine->owned = 1;
    machine->stats.peak_states = machine->stats.peak_slots = 1;
    return true;
}

void machine_destroy(Machine *machine)
{
    free(machine->pool);
    free(machine->private_words);
    free(machine->slots);
    free(machine->ready);
    output_discard(&machine->result);
    output_discard(&machine->trace);
}

void request_stop(Machine *machine, const char *reason)
{
    if (machine->outcome == OUT_RUNNING && !machine->stop_request)
        machine->stop_request = reason;
}

void observe_stop(Machine *machine)
{
    const RunOptions *options = machine->options;
    if (interrupted())
        request_stop(machine, "INTERRUPTED");
    if (options->timeout_set && monotonic_ms() - machine->host_start >= options->timeout_ms)
        request_stop(machine, "WALL_TIME");
    if (options->timed && options->cycle_limit_set && machine->cycle >= options->cycle_limit)
        request_stop(machine, "MODEL_CYCLES");
}

static void result_header(FILE *file, const Machine *machine)
{
    fprintf(file, "BNDETC-RESULT %s\nSTATUS %s\nREASON %s\nPC ",
            BNDETC_FORMAT, outcome_name(machine->outcome), machine->reason);
    if (machine->terminal_pc == NO_PC)
        fputs("NONE", file);
    else
        fprintf(file, "%u", (unsigned)machine->terminal_pc);
    fputs("\nHANDLE ", file);
    if (machine->terminal_handle == NO_HANDLE)
        fputs("NONE\n", file);
    else
        fprintf(file, "%zu\n", machine->terminal_handle);
}

void runtime_fault(Machine *machine, const char *reason)
{
    if (machine->outcome == OUT_RUNNING) {
        set_outcome(machine, OUT_ERROR, reason, NO_HANDLE, NO_PC);
    } else {
        machine->outcome = OUT_ERROR;
        machine->reason = reason;
        machine->result_valid = false;
        event(machine, "runtime_error", NO_HANDLE, NO_HANDLE, NO_PC);
    }
}

void set_outcome(Machine *machine, Outcome outcome, const char *reason,
                 size_t handle, uint32_t pc)
{
    machine->outcome = outcome;
    machine->reason = reason;
    machine->terminal_handle = handle;
    machine->terminal_pc = pc;
    event(machine, "terminal", NO_HANDLE, handle, pc);
    machine->stats.terminal = stamp(machine);
    if (outcome != OUT_ACCEPT)
        return;
    machine->winner = handle;
    machine->pool[handle].role = STATE_RETAINED;
    for (size_t i = 0; i < machine->options->slots; ++i) {
        if (machine->slots[i].handle == handle) {
            clear_slot(&machine->slots[i]);
            --machine->owned;
            break;
        }
    }
    machine->stats.peak_retained = 1;
    event(machine, "winner", NO_HANDLE, handle, pc);
    machine->stats.winner_selected = stamp(machine);
    if (!output_open(&machine->result, machine->options->result_path)) {
        runtime_fault(machine, "OUTPUT_IO");
        return;
    }
    result_header(machine->result.file, machine);
}

Candidates empty_candidates(void)
{
    return (Candidates){.error_handle = NO_HANDLE, .winner = NO_HANDLE,
                        .capacity_handle = NO_HANDLE, .error_pc = NO_PC, .capacity_pc = NO_PC};
}

bool choose_outcome(Machine *machine, const Candidates *candidates, bool exhausted)
{
    if (candidates->error)
        set_outcome(machine, OUT_ERROR, candidates->error,
                    candidates->error_handle, candidates->error_pc);
    else if (candidates->winner != NO_HANDLE)
        set_outcome(machine, OUT_ACCEPT, "STOP", candidates->winner,
                    machine->pool[candidates->winner].pc);
    else if (exhausted)
        set_outcome(machine, OUT_EXHAUSTED, "COMPLETE", NO_HANDLE, NO_PC);
    else if (candidates->capacity_handle != NO_HANDLE)
        set_outcome(machine, OUT_UNKNOWN, "STATE_CAPACITY",
                    candidates->capacity_handle, candidates->capacity_pc);
    else if (machine->stop_request)
        set_outcome(machine, OUT_UNKNOWN, machine->stop_request, NO_HANDLE, NO_PC);
    return machine->outcome != OUT_RUNNING;
}

size_t free_handle(const Machine *machine)
{
    for (size_t i = 0; i < machine->options->states; ++i)
        if (machine->pool[i].role == STATE_FREE)
            return i;
    return NO_HANDLE;
}

static void release_entry(Machine *machine, size_t handle)
{
    if (machine->pool[handle].role == STATE_RESERVED)
        --machine->reserved;
    machine->pool[handle].role = STATE_FREE;
    --machine->used;
}

void reserve_fork(Machine *machine, size_t slot_index, size_t destination)
{
    Slot *slot = &machine->slots[slot_index];
    slot->destination = destination;
    slot->phase = SLOT_COPY_READY;
    machine->pool[destination].role = STATE_RESERVED;
    ++machine->used;
    ++machine->reserved;
    peak(&machine->stats.peak_states, machine->used);
    peak(&machine->stats.peak_reserved, machine->reserved);
    event(machine, "reserve", slot_index, destination, machine->pool[slot->handle].pc);
}

uint16_t read_state_word(const Machine *machine, size_t handle, uint32_t word)
{
    const State *state = &machine->pool[handle];
    if (word == 0)
        return state->pc;
    if (word < 17)
        return state->registers[word - 1];
    return machine->private_words[handle * machine->image->m + word - 17];
}

void write_state_word(Machine *machine, size_t handle, uint32_t word, uint16_t value)
{
    State *state = &machine->pool[handle];
    if (word == 0)
        state->pc = value;
    else if (word < 17)
        state->registers[word - 1] = value;
    else
        machine->private_words[handle * machine->image->m + word - 17] = value;
}

static void complete_native(Machine *machine, size_t slot_index)
{
    Slot *slot = &machine->slots[slot_index];
    increment(machine, &machine->stats.completed[slot->action.opcode], 1);
    event(machine, opcode_names[slot->action.opcode], slot_index, slot->handle,
          machine->pool[slot->handle].pc);
}

void commit_fork(Machine *machine, size_t slot_index)
{
    Slot *slot = &machine->slots[slot_index];
    size_t child = slot->destination;
    State *original = &machine->pool[slot->handle];
    State *alternative = &machine->pool[child];
    complete_native(machine, slot_index);
    original->pc = alternative->pc = slot->action.pc;
    original->registers[slot->action.rd] = 0;
    alternative->registers[slot->action.rd] = 1;
    alternative->role = STATE_READY;
    --machine->reserved;
    machine->ready[machine->ready_count++] = child;
    slot->destination = NO_HANDLE;
    slot->phase = SLOT_FETCH_READY;
    increment(machine, &machine->stats.fork_patch_bytes, 8);
    peak(&machine->stats.peak_ready, machine->ready_count);
    event(machine, "publish_child", slot_index, child, alternative->pc);
}

void assign_ready(Machine *machine, size_t slot_index)
{
    Slot *slot = &machine->slots[slot_index];
    size_t handle = machine->ready[--machine->ready_count];
    slot->handle = handle;
    slot->phase = SLOT_FETCH_READY;
    machine->pool[handle].role = STATE_OWNED;
    ++machine->owned;
    increment(machine, &machine->stats.claims, 1);
    peak(&machine->stats.peak_slots, machine->owned);
    event(machine, "assign", slot_index, handle, machine->pool[handle].pc);
}

void release_failed(Machine *machine, size_t slot_index)
{
    Slot *slot = &machine->slots[slot_index];
    size_t handle = slot->handle;
    uint32_t pc = machine->pool[handle].pc;
    clear_slot(slot);
    --machine->owned;
    release_entry(machine, handle);
    event(machine, "release_failed", slot_index, handle, pc);
}

static Action prepare(const Image *image, const State *state, uint32_t word)
{
    Instruction instruction;
    Action action = {.kind = ACTION_ERROR, .reason = "BAD_ENCODING"};
    uint32_t next = (uint32_t)state->pc + 1;
    const uint16_t *registers = state->registers;
    if (state->pc >= image->n) {
        action.reason = "PC_BOUNDS";
        return action;
    }
    if (!decode_instruction(word, &instruction))
        return action;
    action = (Action){.kind = ACTION_WRITE, .opcode = instruction.opcode, .rd = instruction.rd};
    switch (instruction.opcode) {
    case OP_ADD:
        action.value = (uint16_t)((uint32_t)registers[instruction.ra] + registers[instruction.rb]);
        break;
    case OP_AND:
        action.value = registers[instruction.ra] & registers[instruction.rb];
        break;
    case OP_EQ:
        action.value = registers[instruction.ra] == registers[instruction.rb];
        break;
    case OP_MOVI:
        action.value = instruction.payload;
        break;
    case OP_ADDI:
        action.value = (uint16_t)((uint32_t)registers[instruction.ra] + instruction.payload);
        break;
    case OP_OR:
        action.value = registers[instruction.ra] | registers[instruction.rb];
        break;
    case OP_LT:
        action.value = signed_word(registers[instruction.ra]) < signed_word(registers[instruction.rb]);
        break;
    case OP_LOAD: case OP_STORE:
        action.address = registers[instruction.ra];
        if (action.address >= image->i + image->m) {
            action.kind = ACTION_ERROR;
            action.reason = "DATA_BOUNDS";
            return action;
        }
        if (instruction.opcode == OP_STORE && action.address < image->i) {
            action.kind = ACTION_ERROR;
            action.reason = "READ_ONLY";
            return action;
        }
        action.kind = instruction.opcode == OP_LOAD ? ACTION_LOAD : ACTION_STORE;
        if (action.kind == ACTION_STORE)
            action.value = registers[instruction.rb];
        break;
    case OP_JMP:
        action.kind = ACTION_NEXT;
        next = instruction.rb ? registers[instruction.ra] : instruction.payload;
        break;
    case OP_NOT:
        action.value = (uint16_t)~registers[instruction.ra];
        break;
    case OP_JZ:
        action.kind = ACTION_NEXT;
        if (registers[instruction.ra] == 0)
            next = instruction.payload;
        break;
    case OP_STOP:
        action.kind = ACTION_STOP;
        return action;
    case OP_ASSERT:
        if (registers[instruction.ra] == 0) {
            action.kind = ACTION_FAIL;
            return action;
        }
        action.kind = ACTION_NEXT;
        break;
    case OP_GUESS:
        action.kind = ACTION_GUESS;
        break;
    case OP_RESERVED:
        action.kind = ACTION_ERROR;
        action.reason = "BAD_ENCODING";
        return action;
    }
    if (next >= image->n) {
        action.kind = ACTION_ERROR;
        action.reason = "PC_BOUNDS";
    } else {
        action.pc = (uint16_t)next;
    }
    return action;
}

void finish_fetch(Machine *machine, size_t slot_index, bool ready)
{
    Slot *slot = &machine->slots[slot_index];
    uint32_t pc = machine->pool[slot->handle].pc;
    increment(machine, &machine->stats.fetch_words, 1);
    slot->phase = ready ? SLOT_EXEC_READY : SLOT_PARKED;
    if (ready)
        slot->instruction_word = pc < machine->image->n ? machine->image->code[pc] : 0;
}

void begin_instruction(Machine *machine, size_t slot_index)
{
    Slot *slot = &machine->slots[slot_index];
    const State *state = &machine->pool[slot->handle];
    slot->action = prepare(machine->image, state, slot->instruction_word);
    if (state->pc < machine->image->n) {
        unsigned opcode = slot->instruction_word >> 28;
        increment(machine, &machine->stats.issued[opcode], 1);
    }
    slot->phase = SLOT_EXECUTING;
    event(machine, "issue", slot_index, slot->handle, state->pc);
}

void finish_instruction(Machine *machine, size_t slot_index, Candidates *candidates)
{
    Slot *slot = &machine->slots[slot_index];
    State *state = &machine->pool[slot->handle];
    Action action = slot->action;
    switch (action.kind) {
    case ACTION_LOAD: case ACTION_STORE:
        slot->phase = SLOT_MEMORY_READY;
        return;
    case ACTION_GUESS:
        slot->phase = SLOT_RESERVE;
        return;
    case ACTION_ERROR:
        if (!candidates->error || slot->handle < candidates->error_handle) {
            candidates->error = action.reason;
            candidates->error_handle = slot->handle;
            candidates->error_pc = state->pc;
        }
        slot->phase = SLOT_PARKED;
        return;
    case ACTION_STOP:
        complete_native(machine, slot_index);
        if (slot->handle < candidates->winner)
            candidates->winner = slot->handle;
        if (!machine->stats.first_success.valid)
            machine->stats.first_success = stamp(machine);
        slot->phase = SLOT_PARKED;
        return;
    case ACTION_FAIL:
        complete_native(machine, slot_index);
        increment(machine, &machine->stats.branch_failures, 1);
        slot->phase = SLOT_FAILED;
        return;
    case ACTION_WRITE:
        state->registers[action.rd] = action.value;
        break;
    case ACTION_NEXT:
        break;
    }
    complete_native(machine, slot_index);
    state->pc = action.pc;
    slot->phase = SLOT_FETCH_READY;
}

void finish_memory(Machine *machine, size_t slot_index, bool commit)
{
    Slot *slot = &machine->slots[slot_index];
    State *state = &machine->pool[slot->handle];
    const Image *image = machine->image;
    Action action = slot->action;
    uint16_t word = 0;
    if (action.address < image->i) {
        increment(machine, &machine->stats.input_words, 1);
        word = image->input[action.address];
    } else {
        uint16_t *cell = &machine->private_words[slot->handle * image->m +
                                                action.address - image->i];
        if (action.kind == ACTION_STORE) {
            *cell = action.value;
            increment(machine, &machine->stats.private_writes, 1);
        } else {
            word = *cell;
            increment(machine, &machine->stats.private_reads, 1);
        }
    }
    if (commit) {
        if (action.kind == ACTION_LOAD)
            state->registers[action.rd] = word;
        complete_native(machine, slot_index);
        state->pc = action.pc;
        slot->phase = SLOT_FETCH_READY;
    } else {
        event(machine, "drained_memory", slot_index, slot->handle, state->pc);
        slot->phase = SLOT_PARKED;
    }
}

void begin_drain(Machine *machine)
{
    machine->quiet = true;
    for (size_t i = 0; i < machine->options->slots; ++i)
        clear_slot(&machine->slots[i]);
    machine->ready_count = 0;
    machine->owned = 0;
    event(machine, "quiescent", NO_HANDLE, NO_HANDLE, NO_PC);
    machine->stats.quiescent = stamp(machine);
}

size_t next_reclaim(Machine *machine)
{
    while (machine->reclaim_index < machine->options->states) {
        size_t handle = machine->reclaim_index++;
        StateRole role = machine->pool[handle].role;
        if (role != STATE_FREE && role != STATE_RETAINED)
            return handle;
    }
    if (machine->winner != NO_HANDLE &&
        machine->pool[machine->winner].role == STATE_RETAINED &&
        (machine->outcome != OUT_ACCEPT || machine->result_valid))
        return machine->winner;
    return NO_HANDLE;
}

void reclaim_entry(Machine *machine, size_t handle)
{
    if (machine->pool[handle].role != STATE_RETAINED)
        increment(machine, &machine->stats.cancelled_entries, 1);
    release_entry(machine, handle);
    event(machine, "reclaim", NO_HANDLE, handle, NO_PC);
}

bool next_export(Machine *machine, ExportWord *word)
{
    while (machine->export_reg < 12) {
        unsigned reg = machine->export_reg++;
        if (machine->image->export_registers & (1u << reg)) {
            *word = (ExportWord){true, reg};
            return true;
        }
    }
    while (machine->export_offset < machine->image->m) {
        unsigned offset = machine->export_offset++;
        if (machine->image->export_private[offset]) {
            *word = (ExportWord){false, offset};
            return true;
        }
    }
    return false;
}

void export_word(Machine *machine, ExportWord word)
{
    FILE *file = machine->result.file;
    int rc;
    if (word.is_register) {
        unsigned value = machine->pool[machine->winner].registers[word.index];
        rc = fprintf(file, "REG R%u 0x%04X\n", word.index, value);
    } else {
        unsigned value = read_state_word(machine, machine->winner, 17u + word.index);
        rc = fprintf(file, "MEM %u 0x%04X\n", machine->image->i + word.index, value);
    }
    increment(machine, &machine->stats.export_words, 1);
    if (rc < 0)
        runtime_fault(machine, "OUTPUT_IO");
}

void finish_export(Machine *machine)
{
    if (fputs("END\n", machine->result.file) == EOF || !output_seal(&machine->result)) {
        runtime_fault(machine, "OUTPUT_IO");
        return;
    }
    machine->result_valid = true;
    event(machine, "result_ready", NO_HANDLE, machine->winner, machine->terminal_pc);
    machine->stats.result_ready = stamp(machine);
}

void mark_reclaimed(Machine *machine)
{
    event(machine, "reclaimed", NO_HANDLE, NO_HANDLE, NO_PC);
    machine->stats.reclaimed = stamp(machine);
}

bool save_result(Machine *machine)
{
    if (machine->outcome == OUT_ACCEPT) {
        if (!machine->result_valid)
            return false;
        return output_publish(&machine->result);
    }
    output_discard(&machine->result);
    if (!output_open(&machine->result, machine->options->result_path))
        return false;
    result_header(machine->result.file, machine);
    return fputs("END\n", machine->result.file) != EOF && output_publish(&machine->result);
}
