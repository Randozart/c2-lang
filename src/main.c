// 2026-07-13 — C² compiler CLI entry point.
//   Parses command-line flags, runs the lexer→parser→codegen pipeline,
//   and dispatches to the system C compiler in driver mode.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "typecheck.h"
#include "verify.h"
#include "error.h"

// ── Forward declarations ────────────────────────────────────────────────

static void print_usage(const char* program);
static char* read_file(const char* path, size_t* out_len);
static int   compile_c_file(const char* c_path, const char* output_path);

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

    // Parse flags
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--emit-c") == 0) {
            emit_c_only = 1;
        } else if (input_file == NULL) {
            input_file = argv[i];
        }
    }

    if (input_file == NULL) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Read the source file
    size_t source_len = 0;
    char* source = read_file(input_file, &source_len);
    if (!source) {
        fprintf(stderr, "error: could not read file '%s'\n", input_file);
        return 1;
    }

    // Create error list
    ErrorList* errors = errlist_create();

    if (strcmp(command, "build") == 0) {
        // Parse
        Parser parser = parser_create(source, source_len, input_file, errors);
        AstNode* ast = parser_parse(&parser);

        if (errors->has_errors) {
            errlist_print(errors);
            ast_free_tree(ast);
            free(source);
            errlist_destroy(errors);
            return 2;
        }

        // Type-check
        int tc_errors = typecheck_ast(ast, errors);
        if (errors->has_errors) {
            errlist_print(errors);
        }
        if (tc_errors) {
            ast_free_tree(ast);
            free(source);
            errlist_destroy(errors);
            return 3;
        }

        // Codegen
        Codegen cg;
        codegen_init(&cg, errors);
        codegen_generate(&cg, ast);

        if (errors->has_errors) {
            errlist_print(errors);
            codegen_free(&cg);
            ast_free_tree(ast);
            free(source);
            errlist_destroy(errors);
            return 2;
        }

        // Determine output C path
        char c_path[4096];
        const char* base_name = input_file;
        const char* slash = strrchr(input_file, '/');
        if (slash) base_name = slash + 1;
        slash = strrchr(input_file, '\\');
        if (slash && slash > base_name - 1) base_name = slash + 1;

        mkdir("_c2_out", 0755);

        snprintf(c_path, sizeof(c_path), "_c2_out/%.*s.c",
                 (int)(strlen(base_name) - (strstr(base_name, ".c2") ? 3 : 0)),
                 base_name);

        codegen_write_file(&cg, c_path);
        codegen_free(&cg);

        // Verify derivation examples as a build gate
        int vresult = verify_source(source, source_len, input_file, errors, 0);
        if (vresult > 0) {
            fprintf(stderr, "error: derivation verification failed for '%s'\n", input_file);
            fprintf(stderr, "  Re-run `c2c verify %s` for full details.\n", input_file);
            ast_free_tree(ast);
            free(source);
            errlist_destroy(errors);
            return 1;
        }

        // Driver mode: invoke system C compiler
        if (!emit_c_only) {
            int comp_result = compile_c_file(c_path, output_path);
            if (comp_result != 0) {
                fprintf(stderr, "c2: system compiler invocation failed\n");
                ast_free_tree(ast);
                free(source);
                errlist_destroy(errors);
                return 6;
            }
            printf("c2: built '%s' -> '%s'\n", input_file, output_path);
        } else {
            printf("c2: emitted '%s'\n", c_path);
        }

        ast_free_tree(ast);
    } else if (strcmp(command, "check") == 0) {
        Parser parser = parser_create(source, source_len, input_file, errors);
        AstNode* ast = parser_parse(&parser);

        if (errors->has_errors) {
            errlist_print(errors);
            ast_free_tree(ast);
            free(source);
            errlist_destroy(errors);
            return 2;
        }

        // Type-check
        int tc_errors = typecheck_ast(ast, errors);
        if (errors->has_errors) {
            errlist_print(errors);
        }
        if (tc_errors) {
            ast_free_tree(ast);
            free(source);
            errlist_destroy(errors);
            return 3;
        }

        printf("c2: check passed for '%s'\n", input_file);
        ast_free_tree(ast);
    } else if (strcmp(command, "verify") == 0) {
        int vresult = verify_source(source, source_len, input_file, errors, 1);
        free(source);
        errlist_destroy(errors);
        if (vresult < 0) return 2;
        if (vresult > 0) {
            fprintf(stderr, "c2: verification FAILED\n");
            return 1;
        }
        return 0;
    } else if (strcmp(command, "derive") == 0) {
        printf("c2: derive mode for '%s' (not yet implemented, Phase F)\n", input_file);
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", command);
        print_usage(argv[0]);
        free(source);
        errlist_destroy(errors);
        return 1;
    }

    free(source);
    errlist_destroy(errors);
    return 0;
}

// ── File I/O ────────────────────────────────────────────────────────────

static char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 0) {
        fclose(f);
        return NULL;
    }

    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);

    buf[read] = '\0';
    *out_len = read;
    return buf;
}

static int compile_c_file(const char* c_path, const char* output_path) {
    char cmd[4096];

    // Try GCC first, then Clang
    snprintf(cmd, sizeof(cmd),
        "gcc -std=c23 -Wall -Wextra -O2 '%s' -o '%s' 2>/dev/null || "
        "clang -std=c23 -Wall -Wextra -O2 '%s' -o '%s' 2>/dev/null || "
        "tcc '%s' -o '%s' 2>/dev/null",
        c_path, output_path, c_path, output_path, c_path, output_path);

    return system(cmd);
}

// ── Help text ───────────────────────────────────────────────────────────

static void print_usage(const char* program) {
    fprintf(stderr,
        "c2c — C² (Contract Enforced C) Compiler v1.0\n"
        "\n"
        "Usage:\n"
        "  %s build <file>    Transpile, compile, and verify derivations\n"
        "  %s check <file>    Parse and validate contracts only\n"
        "  %s verify <file>   Run derivation examples as unit tests\n"
        "  %s derive <file>   Synthesize implementations (not yet implemented)\n"
        "\n"
        "Flags:\n"
        "  -o <path>          Output binary path (default: a.out)\n"
        "  --emit-c           Emit C only, do not invoke system compiler\n"
        "\n",
        program, program, program, program);
}
