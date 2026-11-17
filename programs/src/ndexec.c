#include "runtime.h"

#include <inttypes.h>
#include <string.h>

static void usage(void)
{
    puts("Usage: ndexec program.ndp [options]\n"
         "  --backend functional|timed   default functional\n"
         "  --slots P --states K         defaults 4 and 64\n"
         "  --result FILE --stats FILE   defaults result.txt and stats.csv\n"
         "  --trace FILE [--trace-limit N]   optional, default limit 10000 rows\n"
         "  --dump-code FILE             write encoded native words\n"
         "  --timeout-ms N               optional host-time cancellation\n"
         "  --cycle-limit N              optional timed-model cancellation");
}

static bool options_parse(RunOptions *options, int argc, char **argv)
{
    bool trace_limit_set = false;
    *options = (RunOptions){.slots = 4, .states = 64, .trace_limit = 10000,
                           .image_path = argv[1], .result_path = "result.txt",
                           .stats_path = "stats.csv"};
    for (int i = 2; i < argc; ++i) {
        const char *key = argv[i];
        const char *value;
        uint64_t number;
        if (++i == argc)
            return false;
        value = argv[i];
        if (strcmp(key, "--backend") == 0) {
            if (strcmp(value, "timed") == 0)
                options->timed = true;
            else if (strcmp(value, "functional") == 0)
                options->timed = false;
            else
                return false;
        } else if (strcmp(key, "--slots") == 0 || strcmp(key, "--states") == 0) {
            if (!parse_uint(value, SIZE_MAX, &number) || number == 0)
                return false;
            if (strcmp(key, "--slots") == 0)
                options->slots = (size_t)number;
            else
                options->states = (size_t)number;
        } else if (strcmp(key, "--result") == 0) {
            options->result_path = value;
        } else if (strcmp(key, "--stats") == 0) {
            options->stats_path = value;
        } else if (strcmp(key, "--trace") == 0) {
            options->trace_path = value;
        } else if (strcmp(key, "--dump-code") == 0) {
            options->dump_path = value;
        } else if (strcmp(key, "--trace-limit") == 0) {
            if (!parse_uint(value, UINT64_MAX, &options->trace_limit))
                return false;
            trace_limit_set = true;
        } else if (strcmp(key, "--timeout-ms") == 0) {
            if (!parse_uint(value, UINT64_MAX, &options->timeout_ms) || !options->timeout_ms)
                return false;
            options->timeout_set = true;
        } else if (strcmp(key, "--cycle-limit") == 0) {
            if (!parse_uint(value, UINT64_MAX, &options->cycle_limit))
                return false;
            options->cycle_limit_set = true;
        } else {
            return false;
        }
    }
    return options->slots <= options->states && (!trace_limit_set || options->trace_path) &&
           (!options->cycle_limit_set || options->timed);
}

static bool dump_code(const Image *image, const char *path)
{
    Output output;
    bool ok;
    if (!path)
        return true;
    if (!output_open(&output, path))
        return false;
    for (uint32_t pc = 0; pc < image->n; ++pc)
        fprintf(output.file, "%04X %08" PRIX32 "\n", (unsigned)pc, image->code[pc]);
    ok = output_publish(&output);
    output_discard(&output);
    return ok;
}

static void execute(Machine *machine)
{
    const RunOptions *options = machine->options;
    if (options->trace_path) {
        if (!output_open(&machine->trace, options->trace_path)) {
            set_outcome(machine, OUT_ERROR, "TRACE_IO", NO_HANDLE, NO_PC);
            return;
        }
        fputs("event,cycle,kind,slot,handle,pc,occupied,ready,reserved\n", machine->trace.file);
    }
    machine->host_start = monotonic_ms();
    event(machine, "start", 0, 0, 0);
    if (options->timed)
        run_timed(machine);
    else
        run_functional(machine);
    machine->stats.host_ms = monotonic_ms() - machine->host_start;
    if (options->trace_path && !output_publish(&machine->trace))
        runtime_fault(machine, "TRACE_IO");
}

int main(int argc, char **argv)
{
    RunOptions options;
    Image image = {0};
    Machine machine = {.image = &image, .options = &options, .winner = NO_HANDLE,
                       .terminal_handle = NO_HANDLE, .terminal_pc = NO_PC};
    char error[256];
    bool resource_error, result_ok, stats_ok;
    int status;
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        print_version("ndexec");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    }
    if (argc < 2 || !options_parse(&options, argc, argv)) {
        usage();
        return 1;
    }
    const char *paths[] = {options.image_path, options.result_path, options.stats_path,
                           options.trace_path, options.dump_path};
    if (!paths_distinct(paths, sizeof paths / sizeof paths[0]))
        return 1;
    if (!install_stop_handler()) {
        set_outcome(&machine, OUT_ERROR, "CANCEL_INTERFACE", NO_HANDLE, NO_PC);
    } else if (!image_load(&image, options.image_path, error, &resource_error)) {
        fprintf(stderr, "%s: %s\n", options.image_path, error);
        set_outcome(&machine, resource_error ? OUT_UNKNOWN : OUT_ERROR,
                    resource_error ? "ALLOCATION" : "IMAGE_FORMAT", NO_HANDLE, NO_PC);
    } else if (!dump_code(&image, options.dump_path)) {
        set_outcome(&machine, OUT_ERROR, "DUMP_IO", NO_HANDLE, NO_PC);
    } else if (!machine_init(&machine, &image, &options)) {
        set_outcome(&machine, OUT_UNKNOWN, "ALLOCATION", NO_HANDLE, NO_PC);
    } else {
        execute(&machine);
    }
    result_ok = save_result(&machine);
    if (!result_ok)
        runtime_fault(&machine, "OUTPUT_IO");
    stats_ok = save_statistics(&machine);
    printf("%s (%s)\n", outcome_name(machine.outcome), machine.reason);
    status = result_ok && stats_ok ? outcome_exit_code(machine.outcome) : 1;
    machine_destroy(&machine);
    image_free(&image);
    return status;
}
