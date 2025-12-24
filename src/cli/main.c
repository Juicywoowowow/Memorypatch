#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s [options] -- <command> [args...]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o, --output <file>   Specify output file for leak report (default: memorypatch_report.txt)\n");
    fprintf(stderr, "  -h, --help            Show this help message\n");
}

int main(int argc, char** argv) {
    char* output_file = NULL;
    int command_start_index = -1;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            command_start_index = i + 1;
            break;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            // Assume start of command if no '--' is found yet, but usually we enforce '--' or just take the rest
            // For simplicity, let's assume if it's not a flag, it's the command
            command_start_index = i;
            break;
        }
    }

    if (command_start_index == -1 || command_start_index >= argc) {
        fprintf(stderr, "Error: No command specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Prepare LD_PRELOAD
    char self_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len == -1) {
        perror("readlink");
        return 1;
    }
    self_path[len] = '\0';

    char* self_dir = dirname(self_path);
    char lib_path[PATH_MAX];
    // Assuming the lib is in the same directory as the runner (build/)
    snprintf(lib_path, sizeof(lib_path), "%s/libmemorypatch.so", self_dir);

    // Set environment variables
    setenv("LD_PRELOAD", lib_path, 1);
    if (output_file) {
        setenv("MEMORYPATCH_OUTPUT", output_file, 1);
    }

    // Construct argument array for execvp
    char** exec_args = &argv[command_start_index];

    // Execute
    if (execvp(exec_args[0], exec_args) == -1) {
        perror("execvp");
        return 1;
    }

    return 0;
}
