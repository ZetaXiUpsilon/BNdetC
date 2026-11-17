#include "runtime.h"

#include <string.h>

static void fork_functional(Machine *machine, size_t index, Candidates *candidates)
{
    Slot *slot = &machine->slots[index];
    size_t child = free_handle(machine);
    size_t source = slot->handle;
    uint32_t words = 17u + machine->image->m;
    if (child == NO_HANDLE) {
        candidates->capacity_handle = source;
        candidates->capacity_pc = machine->pool[source].pc;
        event(machine, "capacity", index, source, candidates->capacity_pc);
        return;
    }
    reserve_fork(machine, index, child);
    machine->pool[child].pc = machine->pool[source].pc;
    memcpy(machine->pool[child].registers, machine->pool[source].registers,
           sizeof machine->pool[child].registers);
    if (machine->image->m) {
        size_t m = machine->image->m;
        memcpy(machine->private_words + child * m, machine->private_words + source * m,
               m * sizeof *machine->private_words);
    }
    increment(machine, &machine->stats.copies, 1);
    increment(machine, &machine->stats.copy_read_bytes, 2u * words);
    increment(machine, &machine->stats.copy_write_bytes, 2u * words);
    event(machine, "copy_complete", index, child, machine->pool[source].pc);
    commit_fork(machine, index);
}

void run_functional(Machine *machine)
{
    size_t index = 0;
    while (machine->outcome == OUT_RUNNING) {
        Slot *slot = &machine->slots[index];
        Candidates candidates = empty_candidates();
        observe_stop(machine);
        if (choose_outcome(machine, &candidates, machine->used == 0))
            break;
        if (slot->phase == SLOT_EMPTY) {
            if (machine->ready_count) {
                increment(machine, &machine->stats.manager_transactions, 1);
                assign_ready(machine, index);
            }
        } else {
            finish_fetch(machine, index, true);
            begin_instruction(machine, index);
            finish_instruction(machine, index, &candidates);
            if (slot->phase == SLOT_MEMORY_READY) {
                finish_memory(machine, index, true);
            } else if (slot->phase == SLOT_RESERVE) {
                increment(machine, &machine->stats.manager_transactions, 1);
                fork_functional(machine, index, &candidates);
                if (candidates.capacity_handle == NO_HANDLE)
                    increment(machine, &machine->stats.manager_transactions, 1);
            } else if (slot->phase == SLOT_FAILED) {
                increment(machine, &machine->stats.manager_transactions, 1);
                release_failed(machine, index);
            }
            observe_stop(machine);
            choose_outcome(machine, &candidates, machine->used == 0);
        }
        index = (index + 1) % machine->options->slots;
    }

    begin_drain(machine);
    if (machine->outcome == OUT_ACCEPT) {
        ExportWord word;
        while (machine->outcome == OUT_ACCEPT && next_export(machine, &word))
            export_word(machine, word);
        if (machine->outcome == OUT_ACCEPT)
            finish_export(machine);
    }
    for (size_t handle; (handle = next_reclaim(machine)) != NO_HANDLE;) {
        increment(machine, &machine->stats.manager_transactions, 1);
        reclaim_entry(machine, handle);
    }
    mark_reclaimed(machine);
}
