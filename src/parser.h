// 2026-07-13 — Recursive-descent parser for C².
//   Consumes tokens from the lexer and produces an AST.
//   Parses all standard C23 syntax plus C² extensions:
//     contracts, guards, derivation blocks, borrow/own params.

#ifndef C2_PARSER_H
#define C2_PARSER_H

#include "lexer.h"
#include "ast.h"

// ── Parser state ────────────────────────────────────────────────────────

typedef struct {
    Lexer       lexer;
    ErrorList*  errors;
    AstNode*    current_function;  // Function being parsed (for contract/param context)
} Parser;

// ── Parser API ──────────────────────────────────────────────────────────

/// Initialize the parser with a source buffer.
Parser parser_create(const char* source, size_t source_len, const char* filename, ErrorList* errors);

/// Parse the entire translation unit. Returns the root AST node (NODE_TRANSLATION_UNIT).
/// On error, the node may contain partial results; check errors for details.
AstNode* parser_parse(Parser* parser);

#endif // C2_PARSER_H
