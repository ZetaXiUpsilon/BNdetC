#include "runtime.h"

typedef enum { MANAGER_IDLE, CLAIM, RESERVE, PUBLISH, RELEASE, RECLAIM } ManagerKind;
typedef struct {
    ManagerKind kind;
    size_t index;
    uint64_t due;
} Manager;

typedef enum { COPY_IDLE, READ_READY, READING, WRITE_READY, WRITING } CopyPhase;
typedef struct {
    CopyPhase phase;
    size_t slot, source, destination;
    uint32_t index;
    uint16_t buffer;
    uint64_t due;
} Copy;

typedef struct {
    Manager manager;
    Copy copy;
    uint64_t code_free[2], input_free, private_free;
    size_t fetch_cursor, input_cursor, private_cursor, manager_cursor, copy_cursor;
    bool export_pending, export_busy;
    ExportWord word;
    uint64_t export_due;
} Timing;

size_t timing_control_bytes(void)
{
    return sizeof(Timing);
}

static uint64_t after(const Machine *machine, unsigned duration)
{
    return UINT64_MAX - machine->cycle < duration ? UINT64_MAX : machine->cycle + duration;
}

static bool slot_busy(const Slot *slot)
{
    return slot->phase == SLOT_FETCHING || slot->phase == SLOT_EXECUTING ||
           slot->phase == SLOT_MEMORY;
}

static bool copy_busy(const Copy *copy)
{
    return copy->phase == READING || copy->phase == WRITING;
}

static void finish_slots(Machine *machine, Candidates *candidates)
{
    for (size_t i = 0; i < machine->options->slots; ++i) {
        Slot *slot = &machine->slots[i];
        bool running = machine->outcome == OUT_RUNNING;
        if (!slot_busy(slot) || slot->due != machine->cycle)
            continue;
        switch (slot->phase) {
        case SLOT_FETCHING:
            finish_fetch(machine, i, running);
            break;
        case SLOT_EXECUTING:
            if (running)
                finish_instruction(machine, i, candidates);
            else
                slot->phase = SLOT_PARKED;
            break;
        case SLOT_MEMORY:
            finish_memory(machine, i, running);
            break;
        default:
            break;
        }
    }
}

static void finish_copy(Machine *machine, Copy *copy)
{
    if (!copy_busy(copy) || copy->due != machine->cycle)
        return;
    if (copy->phase == READING) {
        copy->buffer = read_state_word(machine, copy->source, copy->index);
        increment(machine, &machine->stats.copy_read_bytes, 2);
        copy->phase = WRITE_READY;
        return;
    }
    write_state_word(machine, copy->destination, copy->index++, copy->buffer);
    increment(machine, &machine->stats.copy_write_bytes, 2);
    if (copy->index == 17u + machine->image->m) {
        increment(machine, &machine->stats.copies, 1);
        event(machine, "copy_complete", copy->slot, copy->destination,
              machine->pool[copy->source].pc);
        machine->slots[copy->slot].phase = SLOT_FORK_READY;
        copy->phase = COPY_IDLE;
    } else {
        copy->phase = READ_READY;
    }
}

static Manager finish_manager(Machine *machine, Timing *timing)
{
    Manager operation = timing->manager;
    if (operation.kind == MANAGER_IDLE || operation.due != machine->cycle)
        return (Manager){0};
    timing->manager.kind = MANAGER_IDLE;
    increment(machine, &machine->stats.manager_transactions, 1);
    if (operation.kind == RECLAIM)
        reclaim_entry(machine, operation.index);
    else if (operation.kind == RELEASE && machine->outcome == OUT_RUNNING)
        release_failed(machine, operation.index);
    return operation;
}

static size_t find_slot(const Machine *machine, SlotPhase phase, size_t *cursor)
{
    size_t index = *cursor;
    for (size_t count = 0; count < machine->options->slots; ++count) {
        size_t next = (index + 1) % machine->options->slots;
        if (machine->slots[index].phase == phase) {
            *cursor = next;
            return index;
        }
        index = next;
    }
    return NO_HANDLE;
}

static void start_fetches(Machine *machine, Timing *timing)
{
    for (unsigned port = 0; port < 2; ++port) {
        size_t index;
        if (timing->code_free[port] > machine->cycle)
            continue;
        index = find_slot(machine, SLOT_FETCH_READY, &timing->fetch_cursor);
        if (index == NO_HANDLE)
            break;
        machine->slots[index].phase = SLOT_FETCHING;
        timing->code_free[port] = machine->slots[index].due = after(machine, FETCH_CYCLES);
    }
}

static void start_memory(Machine *machine, uint64_t *port, size_t *cursor, bool input)
{
    size_t index = *cursor;
    if (*port > machine->cycle)
        return;
    for (size_t count = 0; count < machine->options->slots; ++count) {
        Slot *slot = &machine->slots[index];
        size_t next = (index + 1) % machine->options->slots;
        if (slot->phase == SLOT_MEMORY_READY && (slot->action.address < machine->image->i) == input) {
            slot->phase = SLOT_MEMORY;
            *port = slot->due = after(machine, MEMORY_CYCLES);
            *cursor = next;
            return;
        }
        index = next;
    }
}

static void start_manager(Machine *machine, Timing *timing)
{
    size_t index = timing->manager_cursor;
    if (timing->manager.kind != MANAGER_IDLE)
        return;
    for (size_t count = 0; count < machine->options->slots; ++count) {
        SlotPhase phase = machine->slots[index].phase;
        ManagerKind kind = MANAGER_IDLE;
        size_t next = (index + 1) % machine->options->slots;
        if (phase == SLOT_RESERVE)
            kind = RESERVE;
        else if (phase == SLOT_FORK_READY)
            kind = PUBLISH;
        else if (phase == SLOT_FAILED)
            kind = RELEASE;
        else if (phase == SLOT_EMPTY && machine->ready_count)
            kind = CLAIM;
        if (kind != MANAGER_IDLE) {
            timing->manager = (Manager){kind, index, after(machine, MANAGER_CYCLES)};
            timing->manager_cursor = next;
            return;
        }
        index = next;
    }
}

static void start_copy(Machine *machine, Timing *timing)
{
    Copy *copy = &timing->copy;
    if (copy->phase == COPY_IDLE) {
        size_t index = find_slot(machine, SLOT_COPY_READY, &timing->copy_cursor);
        Slot *slot;
        if (index == NO_HANDLE)
            return;
        slot = &machine->slots[index];
        *copy = (Copy){.phase = READ_READY, .slot = index, .source = slot->handle,
                       .destination = slot->destination};
        slot->phase = SLOT_COPYING;
        event(machine, "copy_begin", index, copy->destination, machine->pool[slot->handle].pc);
    }
    if (copy->phase == READ_READY || copy->phase == WRITE_READY) {
        copy->phase = copy->phase == READ_READY ? READING : WRITING;
        copy->due = after(machine, COPY_CYCLES);
    }
}

static void start_search_work(Machine *machine, Timing *timing)
{
    for (size_t i = 0; i < machine->options->slots; ++i) {
        if (machine->slots[i].phase == SLOT_EXEC_READY) {
            begin_instruction(machine, i);
            machine->slots[i].due = after(machine, ALU_CYCLES);
        }
    }
    start_fetches(machine, timing);
    start_memory(machine, &timing->input_free, &timing->input_cursor, true);
    start_memory(machine, &timing->private_free, &timing->private_cursor, false);
    start_manager(machine, timing);
    start_copy(machine, timing);
}

static bool search_busy(const Machine *machine, const Timing *timing)
{
    if (copy_busy(&timing->copy) ||
        (timing->manager.kind != MANAGER_IDLE && timing->manager.kind != RECLAIM))
        return true;
    for (size_t i = 0; i < machine->options->slots; ++i)
        if (slot_busy(&machine->slots[i]))
            return true;
    return false;
}

static void service_export(Machine *machine, Timing *timing)
{
    uint64_t due;
    if (machine->outcome != OUT_ACCEPT || machine->result_valid || timing->export_busy)
        return;
    if (!timing->export_pending) {
        timing->export_pending = next_export(machine, &timing->word);
        if (!timing->export_pending) {
            finish_export(machine);
            return;
        }
    }
    if (timing->word.is_register) {
        due = after(machine, EXPORT_REGISTER_CYCLES);
    } else {
        if (timing->private_free > machine->cycle)
            return;
        timing->private_free = due = after(machine, MEMORY_CYCLES);
    }
    timing->export_due = due;
    timing->export_busy = true;
    timing->export_pending = false;
}

static bool drain(Machine *machine, Timing *timing)
{
    if (timing->copy.phase != COPY_IDLE && !copy_busy(&timing->copy)) {
        event(machine, "copy_cancel", timing->copy.slot, timing->copy.destination, NO_PC);
        timing->copy.phase = COPY_IDLE;
    }
    if (!machine->quiet && !search_busy(machine, timing))
        begin_drain(machine);
    service_export(machine, timing);
    if (machine->quiet && timing->manager.kind == MANAGER_IDLE) {
        size_t handle = next_reclaim(machine);
        if (handle != NO_HANDLE)
            timing->manager = (Manager){RECLAIM, handle, after(machine, MANAGER_CYCLES)};
    }
    return machine->quiet && machine->used == 0 && timing->manager.kind == MANAGER_IDLE &&
           (machine->outcome != OUT_ACCEPT || machine->result_valid);
}

static void earlier(uint64_t *next, bool *found, uint64_t candidate)
{
    if (!*found || candidate < *next)
        *next = candidate;
    *found = true;
}

static bool next_time(const Machine *machine, const Timing *timing, uint64_t *next)
{
    bool found = false;
    for (size_t i = 0; i < machine->options->slots; ++i) {
        const Slot *slot = &machine->slots[i];
        if (slot_busy(slot))
            earlier(next, &found, slot->due);
    }
    if (copy_busy(&timing->copy))
        earlier(next, &found, timing->copy.due);
    if (timing->manager.kind != MANAGER_IDLE)
        earlier(next, &found, timing->manager.due);
    if (timing->export_busy)
        earlier(next, &found, timing->export_due);
    if (machine->outcome == OUT_RUNNING && machine->options->cycle_limit_set &&
        machine->options->cycle_limit > machine->cycle)
        earlier(next, &found, machine->options->cycle_limit);
    return found && *next > machine->cycle;
}

static void advance_clock(Machine *machine, const Timing *timing, uint64_t next)
{
    uint64_t delta = next - machine->cycle;
    Statistics *stats = &machine->stats;
    for (size_t i = 0; i < machine->options->slots; ++i) {
        const Slot *slot = &machine->slots[i];
        if (slot->handle != NO_HANDLE)
            increment(machine, &stats->owned_slot_cycles, delta);
        if (slot->phase == SLOT_EXECUTING)
            increment(machine, &stats->alu_slot_cycles, delta);
        if (machine->outcome != OUT_RUNNING)
            continue;
        switch (slot->phase) {
        case SLOT_FETCH_READY:
            increment(machine, &stats->fetch_wait_cycles, delta);
            break;
        case SLOT_MEMORY_READY:
            increment(machine, slot->action.address < machine->image->i ?
                      &stats->input_wait_cycles : &stats->private_wait_cycles, delta);
            break;
        case SLOT_COPY_READY:
            increment(machine, &stats->copy_wait_cycles, delta);
            break;
        case SLOT_RESERVE: case SLOT_FORK_READY: case SLOT_FAILED:
            if (timing->manager.kind == MANAGER_IDLE || timing->manager.index != i)
                increment(machine, &stats->manager_wait_cycles, delta);
            break;
        default:
            break;
        }
    }
    if (copy_busy(&timing->copy))
        increment(machine, &stats->copy_busy_cycles, delta);
    if (timing->manager.kind != MANAGER_IDLE)
        increment(machine, &stats->manager_busy_cycles, delta);
    machine->cycle = next;
}

void run_timed(Machine *machine)
{
    Timing timing = {0};
    for (;;) {
        Candidates candidates = empty_candidates();
        Manager complete;
        uint64_t next;
        finish_slots(machine, &candidates);
        finish_copy(machine, &timing.copy);
        complete = finish_manager(machine, &timing);
        if (timing.export_busy && timing.export_due == machine->cycle) {
            timing.export_busy = false;
            if (machine->outcome == OUT_ACCEPT)
                export_word(machine, timing.word);
        }
        if (machine->cycle == UINT64_MAX) {
            runtime_fault(machine, "MODEL_CLOCK_RANGE");
            break;
        }
        if (machine->outcome == OUT_RUNNING) {
            size_t destination = NO_HANDLE;
            if (complete.kind == RESERVE) {
                Slot *slot = &machine->slots[complete.index];
                destination = free_handle(machine);
                if (destination == NO_HANDLE) {
                    candidates.capacity_handle = slot->handle;
                    candidates.capacity_pc = machine->pool[slot->handle].pc;
                    event(machine, "capacity", complete.index, slot->handle, candidates.capacity_pc);
                }
            }
            observe_stop(machine);
            if (!choose_outcome(machine, &candidates, machine->used == 0 &&
                                !search_busy(machine, &timing))) {
                if (complete.kind == CLAIM)
                    assign_ready(machine, complete.index);
                else if (complete.kind == RESERVE)
                    reserve_fork(machine, complete.index, destination);
                else if (complete.kind == PUBLISH)
                    commit_fork(machine, complete.index);
                start_search_work(machine, &timing);
            }
        }
        if (machine->outcome != OUT_RUNNING && drain(machine, &timing)) {
            mark_reclaimed(machine);
            return;
        }
        if (!next_time(machine, &timing, &next)) {
            runtime_fault(machine, "SCHEDULER_STALLED");
            break;
        }
        advance_clock(machine, &timing, next);
    }

    /* The event loop reached its representable limit or has no pending event. */
    if (!machine->quiet)
        begin_drain(machine);
    for (size_t handle; (handle = next_reclaim(machine)) != NO_HANDLE;)
        reclaim_entry(machine, handle);
    mark_reclaimed(machine);
}
