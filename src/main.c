// 2026-07-13 — C² compiler CLI entry point.
//   Parses command-line flags and dispatches to the appropriate
//   pipeline phase: build, check, or derive.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "error.h"

// ── Forward declarations ────────────────────────────────────────────────

static void print_usage(const char* program);

// ── Entry point ─────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* command = argv[1];
    const char* input_file = NULL;
    const char* output_path = "a.out";
    int emit_c_only = 0;
    int check_only = 0;

    // Parse flags
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--emit-c") == 0) {
            emit_c_only = 1;
        } else if (strcmp(argv[i], "--check-only") == 0) {
            check_only = 1;
        } else if (input_file == NULL) {
            input_file = argv[i];
        }
    }

    if (input_file == NULL) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Create error list
    ErrorList* errors = errlist_create();

    // Dispatch commands
    if (strcmp(command, "build") == 0) {
        printf("c2: building '%s'...\n", input_file);
        printf("c2: (compiler implementation pending Phase A)\n");
    } else if (strcmp(command, "check") == 0) {
        printf("c2: checking '%s'...\n", input_file);
        printf("c2: (compiler implementation pending Phase A)\n");
    } else if (strcmp(command, "derive") == 0) {
        printf("c2: deriving '%s'...\n", input_file);
        printf("c2: (compiler implementation pending Phase F)\n");
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", command);
        print_usage(argv[0]);
        errlist_destroy(errors);
        return 1;
    }

    errlist_destroy(errors);
    return 0;
}

// ── Help text ───────────────────────────────────────────────────────────

static void print_usage(const char* program) {
    fprintf(stderr,
        "C² (Contract Enforced C) — Compiler v1.0\n"
        "\n"
        "Usage:\n"
        "  %s build <file>    Transpile and compile to binary\n"
        "  %s check <file>    Verify contracts only\n"
        "  %s derive <file>   Synthesize implementations\n"
        "\n"
        "Flags:\n"
        "  -o <path>          Output binary path (default: a.out)\n"
        "  --emit-c           Emit C only, do not invoke system compiler\n"
        "  --check-only       Verify only, no codegen\n"
        "\n",
        program, program, program);
}
