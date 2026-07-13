// 2026-07-13 — Type checker for C².
//   Walks the AST and annotates each node with its inferred Type*.
//   Reports type errors, contract arity errors, and ownership modifier errors.

#ifndef C2_TYPECHECK_H
#define C2_TYPECHECK_H

#include "ast.h"
#include "symbol.h"
#include "error.h"

/// Run the type checker on a fully parsed AST.
/// Populates node->type for all expression nodes.
/// Reports errors via the ErrorList.
/// Returns 0 on success (no type errors), 1 on type errors.
int typecheck_ast(AstNode* root, ErrorList* errors);

#endif // C2_TYPECHECK_H
