#include "runtime.h"

#include <inttypes.h>

static void metric(FILE *file, const char *name, uint64_t value)
{
    fprintf(file, "%s,%" PRIu64 "\n", name, value);
}

static void time_stamp(FILE *file, const char *name, Stamp stamp, bool timed)
{
    fprintf(file, "%s_event,", name);
    if (stamp.valid)
        fprintf(file, "%" PRIu64, stamp.event);
    fprintf(file, "\n%s_cycle,", name);
    if (stamp.valid && timed)
        fprintf(file, "%" PRIu64, stamp.cycle);
    fputc('\n', file);
}

bool save_statistics(const Machine *machine)
{
    const RunOptions *options = machine->options;
    const Statistics *stats = &machine->stats;
    Output output;
    FILE *file;
    bool ok;
    if (!output_open(&output, options->stats_path))
        return false;
    file = output.file;
    fputs("metric,value\nimage_path,", file);
    csv_string(file, options->image_path);
    fprintf(file, "\nversion,%s\nisa,%s\nformat,%s\nbackend,%s\n",
            BNDETC_VERSION, BNDETC_FORMAT, BNDETC_FORMAT, options->timed ? "timed" : "functional");
    fprintf(file, "status,%s\nreason,%s\nimage_fingerprint,%016" PRIx64 "\n",
            outcome_name(machine->outcome), machine->reason, machine->image->fingerprint);
    metric(file, "P", options->slots);
    metric(file, "K", options->states);
    metric(file, "M", machine->image->m);
    metric(file, "I", machine->image->i);
    metric(file, "N", machine->image->n);
    metric(file, "state_payload_bytes", 34u + 2u * machine->image->m);
    metric(file, "logical_bytes_with_control", machine->logical_bytes);
    metric(file, "tracked_c_bytes", machine->allocated_bytes);
    metric(file, "timeout_ms", options->timeout_set ? options->timeout_ms : 0);
    if (options->cycle_limit_set)
        metric(file, "cycle_limit", options->cycle_limit);
    else
        fputs("cycle_limit,\n", file);
    metric(file, "trace_limit", options->trace_path ? options->trace_limit : 0);
    for (unsigned i = 0; i < 16; ++i) {
        if (i == OP_RESERVED)
            continue;
        fprintf(file, "issued_%s,%" PRIu64 "\ncompleted_%s,%" PRIu64 "\n",
                opcode_names[i], stats->issued[i], opcode_names[i], stats->completed[i]);
    }
#define STAT(name) metric(file, #name, stats->name)
    STAT(events);
    STAT(fetch_words);
    STAT(input_words);
    STAT(private_reads);
    STAT(private_writes);
    STAT(copies);
    STAT(copy_read_bytes);
    STAT(copy_write_bytes);
    STAT(fork_patch_bytes);
    STAT(claims);
    STAT(branch_failures);
    STAT(manager_transactions);
    STAT(export_words);
    STAT(cancelled_entries);
    STAT(peak_states);
    STAT(peak_slots);
    STAT(peak_ready);
    STAT(peak_reserved);
    STAT(peak_retained);
    STAT(trace_rows);
    STAT(trace_truncated);
    STAT(saturated);
    STAT(host_ms);
    if (options->timed) {
        fputs("timing_profile,reference-1\n", file);
        metric(file, "fetch_ports", 2);
        metric(file, "fetch_cycles", FETCH_CYCLES);
        metric(file, "alu_cycles", ALU_CYCLES);
        metric(file, "memory_cycles", MEMORY_CYCLES);
        metric(file, "copy_transfer_cycles", COPY_CYCLES);
        metric(file, "manager_cycles", MANAGER_CYCLES);
        metric(file, "export_register_cycles", EXPORT_REGISTER_CYCLES);
        metric(file, "total_cycles", machine->cycle);
        STAT(alu_slot_cycles);
        STAT(owned_slot_cycles);
        STAT(fetch_wait_cycles);
        STAT(input_wait_cycles);
        STAT(private_wait_cycles);
        STAT(copy_wait_cycles);
        STAT(manager_wait_cycles);
        STAT(manager_busy_cycles);
        STAT(copy_busy_cycles);
        if (stats->terminal.valid && stats->quiescent.valid)
            metric(file, "cancel_tail_cycles", stats->quiescent.cycle - stats->terminal.cycle);
    }
#undef STAT
    time_stamp(file, "first_success", stats->first_success, options->timed);
    time_stamp(file, "winner_selected", stats->winner_selected, options->timed);
    time_stamp(file, "terminal", stats->terminal, options->timed);
    time_stamp(file, "result_ready", stats->result_ready, options->timed);
    time_stamp(file, "quiescent", stats->quiescent, options->timed);
    time_stamp(file, "reclaimed", stats->reclaimed, options->timed);
    ok = output_publish(&output);
    output_discard(&output);
    return ok;
}
