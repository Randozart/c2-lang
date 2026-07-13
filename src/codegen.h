// 2026-07-13 — C code generator for C².
//   Walks a verified AST and emits standard C23 code.
//   Strips C²-specific constructs (contracts, borrow/own, when)
//   and converts them to standard C equivalents.

#ifndef C2_CODEGEN_H
#define C2_CODEGEN_H

#include "ast.h"
#include "error.h"

// ── Codegen state ───────────────────────────────────────────────────────

typedef struct {
    char*   output;
    size_t  output_len;
    size_t  output_cap;
    int     indent_level;
    ErrorList* errors;
} Codegen;

// ── Codegen API ─────────────────────────────────────────────────────────

/// Initialize the codegen state.
void codegen_init(Codegen* cg, ErrorList* errors);

/// Free codegen resources.
void codegen_free(Codegen* cg);

/// Generate C source code from the AST.
/// Returns the generated C source as a null-terminated string.
/// The string is owned by the Codegen struct and freed in codegen_free().
const char* codegen_generate(Codegen* cg, AstNode* root);

/// Write the generated code to a file.
/// Returns 0 on success, -1 on error.
int codegen_write_file(Codegen* cg, const char* path);

#endif // C2_CODEGEN_H
