// 2026-07-13 — Drop injection pass for C².
//   Inserts NODE_DROP_CALL nodes at block/function scope boundaries
//   for variables that are still in OWNED state. Skips variables
//   already transitioned to STATE_DROPPED (early free).

#ifndef C2_DROP_H
#define C2_DROP_H

#include "ast.h"
#include "symbol.h"
#include "error.h"

/// Run the drop injection pass on a typed AST.
/// Mutates the AST by adding NODE_DROP_CALL children at scope exits.
/// Returns 0 on success, non-zero on error.
int drop_inject(AstNode* root, SymbolTable* symtab, ErrorList* errors);

#endif