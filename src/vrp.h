// 2026-07-13 — Value Range Propagation (VRP) for C².
//   Infers integer variable ranges from assignments, loop boundaries,
//   and when-guard conditions. Populates node->range on AST variable
//   references and sym->range on symbol table entries.

#ifndef C2_VRP_H
#define C2_VRP_H

#include "ast.h"
#include "error.h"
#include "symbol.h"

/// Run value range propagation on the AST.
/// Walks the tree, tracks variable ranges through assignments and
/// control flow, and populates the `range` field on variable nodes.
/// Returns 0 on success, non-zero on error.
int vrp_run(AstNode* root, SymbolTable* symtab, ErrorList* errors);

#endif