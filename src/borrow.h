// 2026-07-13 — Lexical borrow checker for C².
//   Validates variable state transitions (read, write, move, borrow, drop)
//   using the 5-state machine. Reports use-after-move, use-after-drop,
//   mutate-while-borrowed, and other ownership violations.

#ifndef C2_BORROW_H
#define C2_BORROW_H

#include "ast.h"
#include "symbol.h"
#include "error.h"

/// Run the borrow checker on a fully typed AST.
/// Walks each function body and validates variable state transitions.
/// Reports errors via the ErrorList.
/// Returns 0 if all checks pass, 1 on borrow errors.
int borrow_check(AstNode* root, SymbolTable* symtab, ErrorList* errors);

#endif