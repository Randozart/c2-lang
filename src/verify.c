// 2026-07-13 — C² derivation verification implementation.
//   For each function with both a body and a derivation block,
//   generates a C test harness, compiles it with the transpiled
//   code, runs the binary, and reports pass/fail per example.

#define _POSIX_C_SOURCE 200809L
#include "verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "error.h"

// ── Helpers ───────────────────────────────────────────────────────────────

/// Count how many functions in the translation unit have both
/// a body AND a derivation block.
static int count_verifiable_functions(AstNode* root) {
    if (!root || root->kind != NODE_TRANSLATION_UNIT) return 0;
    int count = 0;
    for (size_t i = 0; i < root->child_count; i++) {
        AstNode* child = root->children[i];
        if (child->kind != NODE_FUNCTION) continue;
        int has_body = 0;
        int has_deriv = 0;
        for (size_t j = 0; j < child->child_count; j++) {
            if (child->children[j]->kind == NODE_BLOCK) has_body = 1;
            if (child->children[j]->kind == NODE_DERIVATION) has_deriv = 1;
        }
        if (has_body && has_deriv) count++;
    }
    return count;
}

/// Emit an expression node as C source text to a file.
/// Handles the common literal/identifier types used in derivation examples.
static void emit_expr_as_c(FILE* f, AstNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NODE_LITERAL_INT:
            fprintf(f, "%" PRId64, node->token.value.i64);
            break;
        case NODE_LITERAL_FLOAT:
            fprintf(f, "%g", node->token.value.f64);
            break;
        case NODE_LITERAL_CHAR:
            fprintf(f, "%.*s", (int)node->token.len, node->token.text);
            break;
        case NODE_LITERAL_STR:
            fprintf(f, "%.*s", (int)node->token.len, node->token.text);
            break;
        case NODE_VARIABLE:
            fprintf(f, "%.*s", (int)node->token.len, node->token.text);
            break;
        case NODE_UNARY_OP: {
            const char* op = token_kind_name(node->token.kind);
            fprintf(f, "%s", op);
            if (node->child_count > 0) emit_expr_as_c(f, node->children[0]);
            break;
        }
        case NODE_BINARY_OP: {
            if (node->child_count >= 2) {
                fprintf(f, "(");
                emit_expr_as_c(f, node->children[0]);
                fprintf(f, " %s ", token_kind_name(node->token.kind));
                emit_expr_as_c(f, node->children[1]);
                fprintf(f, ")");
            }
            break;
        }
        default:
            fprintf(f, "%.*s", (int)node->token.len, node->token.text);
            break;
    }
}

/// Emit the C representation of a type node to a file.
/// Mirrors codegen's type emission for consistency.
static void emit_type_as_c(FILE* f, AstNode* node) {
    if (!node) return;
    if (node->kind == NODE_VARIABLE) {
        fprintf(f, "%.*s", (int)node->token.len, node->token.text);
        // struct/union/enum with tag: emit the tag name after the keyword
        if ((node->token.kind == TOK_STRUCT || node->token.kind == TOK_UNION ||
             node->token.kind == TOK_ENUM) && node->child_count > 0) {
            fprintf(f, " ");
            emit_type_as_c(f, node->children[0]);
        }
    } else if (node->kind == NODE_DECL && node->child_count > 0) {
        emit_type_as_c(f, node->children[0]);
    } else {
        fprintf(f, "%.*s", (int)node->token.len, node->token.text);
    }
}

/// Emit a full function prototype (for extern declaration) to a file.
static void emit_function_prototype(FILE* f, AstNode* func) {
    // Return type
    for (size_t i = 0; i < func->child_count; i++) {
        AstNode* child = func->children[i];
        if (child->kind == NODE_BLOCK || child->kind == NODE_DERIVATION ||
            child->kind == NODE_CONTRACT_PRE || child->kind == NODE_CONTRACT_POST ||
            child->kind == NODE_PARAM_LIST) {
            continue;
        }
        emit_type_as_c(f, child);
        break;
    }
    fprintf(f, " %.*s(", (int)func->token.len, func->token.text);

    // Parameters
    for (size_t i = 0; i < func->child_count; i++) {
        if (func->children[i]->kind != NODE_PARAM_LIST) continue;
        AstNode* params = func->children[i];
        for (size_t j = 0; j < params->child_count; j++) {
            if (j > 0) fprintf(f, ", ");
            AstNode* param = params->children[j];
            if (param->kind == NODE_DECL) {
                emit_type_as_c(f, param);
                fprintf(f, " %.*s", (int)param->token.len, param->token.text);
            }
        }
    }
    fprintf(f, ")");
}

/// Generate the test harness C file.
/// Returns 0 on success, -1 on error.
static int generate_test_harness(const char* harness_path, AstNode* root) {
    FILE* f = fopen(harness_path, "w");
    if (!f) return -1;

    fprintf(f, "// Generated by c2c verify — derivation test harness\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <stdint.h>\n\n");

    // Extern declarations and test groups per function
    for (size_t i = 0; i < root->child_count; i++) {
        AstNode* func = root->children[i];
        if (func->kind != NODE_FUNCTION) continue;

        AstNode* deriv = NULL;
        int has_body = 0;
        for (size_t j = 0; j < func->child_count; j++) {
            if (func->children[j]->kind == NODE_BLOCK) has_body = 1;
            if (func->children[j]->kind == NODE_DERIVATION) deriv = func->children[j];
        }
        if (!has_body || !deriv) continue;

        // Extern declaration
        fprintf(f, "extern ");
        emit_function_prototype(f, func);
        fprintf(f, ";\n\n");
    }

    fprintf(f, "int main(void) {\n");
    fprintf(f, "    int failures = 0;\n");
    fprintf(f, "    int total = 0;\n\n");

    for (size_t i = 0; i < root->child_count; i++) {
        AstNode* func = root->children[i];
        if (func->kind != NODE_FUNCTION) continue;

        AstNode* deriv = NULL;
        int has_body = 0;
        for (size_t j = 0; j < func->child_count; j++) {
            if (func->children[j]->kind == NODE_BLOCK) has_body = 1;
            if (func->children[j]->kind == NODE_DERIVATION) deriv = func->children[j];
        }
        if (!has_body || !deriv) continue;

        fprintf(f, "    // ── %.*s ──\n", (int)func->token.len, func->token.text);

        for (size_t j = 0; j < deriv->child_count; j++) {
            AstNode* example = deriv->children[j];
            if (example->kind != NODE_DERIV_EXAMPLE) continue;

            size_t num_inputs = example->child_count > 0 ? example->child_count - 1 : 0;
            AstNode* expected = num_inputs < example->child_count
                              ? example->children[num_inputs] : NULL;

            fprintf(f, "    {\n");
            fprintf(f, "        total++;\n");

            // Build the function call as a string
            fprintf(f, "        int64_t result = (int64_t)");
            fprintf(f, "%.*s", (int)func->token.len, func->token.text);
            fprintf(f, "(");
            for (size_t k = 0; k < num_inputs; k++) {
                if (k > 0) fprintf(f, ", ");
                emit_expr_as_c(f, example->children[k]);
            }
            fprintf(f, ");\n");

            // Build the expected value
            fprintf(f, "        int64_t expected = (int64_t)");
            if (expected) emit_expr_as_c(f, expected);
            fprintf(f, ";\n");

            // Build a description string for this example
            fprintf(f, "        char desc[256];\n");
            fprintf(f, "        snprintf(desc, sizeof(desc), \"%.*s(",
                    (int)func->token.len, func->token.text);
            for (size_t k = 0; k < num_inputs; k++) {
                if (k > 0) fprintf(f, ", ");
                emit_expr_as_c(f, example->children[k]);
            }
            fprintf(f, ")\");\n");

            // Check and report
            fprintf(f, "        if (result != expected) {\n");
            fprintf(f, "            printf(\"FAIL: %%s expected %%lld, got %%lld\\n\", desc, (long long)expected, (long long)result);\n");
            fprintf(f, "            failures++;\n");
            fprintf(f, "        } else {\n");
            fprintf(f, "            printf(\"PASS: %%s == %%lld\\n\", desc, (long long)expected);\n");
            fprintf(f, "        }\n");
            fprintf(f, "    }\n\n");
        }
    }

    fprintf(f, "    printf(\"verify: %%d/%%d derivations passed\\n\", total - failures, total);\n");
    fprintf(f, "    return failures;\n");
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────

int verify_source(const char* source, size_t source_len,
                  const char* filename, ErrorList* errors,
                  int print_output) {
    // Parse
    Parser parser = parser_create(source, source_len, filename, errors);
    AstNode* ast = parser_parse(&parser);
    if (errors->has_errors) {
        errlist_print(errors);
        return -1;
    }

    // Count functions with derivations
    int nfuncs = count_verifiable_functions(ast);
    if (nfuncs == 0) {
        if (print_output) printf("No derivation examples to verify.\n");
        ast_free_tree(ast);
        return 0;
    }

    // Codegen transpiled C (skip user's main() to avoid linker conflict)
    Codegen cg;
    codegen_init(&cg, errors);
    cg.skip_main = 1;
    codegen_generate(&cg, ast);

    if (errors->has_errors) {
        errlist_print(errors);
        codegen_free(&cg);
        ast_free_tree(ast);
        return -1;
    }

    // Write transpiled C
    mkdir("_c2_out", 0755);
    char c_path[4096];
    const char* base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    snprintf(c_path, sizeof(c_path), "_c2_out/verify_%.*s.c",
             (int)(strstr(base, ".c2") ? strstr(base, ".c2") - base : (int)strlen(base)),
             base);

    codegen_write_file(&cg, c_path);
    codegen_free(&cg);

    // Generate test harness
    char harness_path[4096];
    snprintf(harness_path, sizeof(harness_path), "_c2_out/verify_harness_%.*s.c",
             (int)(strstr(base, ".c2") ? strstr(base, ".c2") - base : (int)strlen(base)),
             base);

    if (generate_test_harness(harness_path, ast) != 0) {
        fprintf(stderr, "error: could not write test harness\n");
        ast_free_tree(ast);
        return -1;
    }

    if (print_output) {
        printf("\nVerifying %d function(s)...\n\n", nfuncs);
    }

    // Compile transpiled C + test harness
    char bin_path[4096];
    snprintf(bin_path, sizeof(bin_path), "_c2_out/verify_bin_%.*s",
             (int)(strstr(base, ".c2") ? strstr(base, ".c2") - base : (int)strlen(base)),
             base);

    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
             "gcc -std=c23 -Wall -Wextra -O2 -Iinclude '%s' '%s' -o '%s' 2>&1",
             c_path, harness_path, bin_path);

    int compile_result = system(cmd);
    if (compile_result != 0) {
        fprintf(stderr, "error: verification compilation failed\n");
        ast_free_tree(ast);
        return -1;
    }

    // Run the verification binary
    int result = 0;
    FILE* verify_out = popen(bin_path, "r");
    if (!verify_out) {
        fprintf(stderr, "error: could not run verification binary\n");
        ast_free_tree(ast);
        return -1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), verify_out)) {
        if (print_output) {
            // Print PASS/FAIL lines with indentation
            if (strncmp(line, "PASS:", 5) == 0 || strncmp(line, "FAIL:", 5) == 0) {
                printf("  %s", line);
            } else if (strncmp(line, "verify:", 7) == 0) {
                printf("\n%s", line);
            }
        }
    }

    int exit_code = pclose(verify_out);
    if (WIFEXITED(exit_code)) {
        result = WEXITSTATUS(exit_code);
    } else {
        result = 1;
    }

    // Cleanup
    ast_free_tree(ast);

    if (print_output) {
        printf("\n");
    }

    return result;
}
