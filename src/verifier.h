// 2026-07-13 — Z3 contract verifier for C².
//   Translates contract pre/post conditions into Z3 bit-vector formulas
//   and runs proof queries to verify them at compile time.

#ifndef C2_VERIFIER_H
#define C2_VERIFIER_H

#include "ast.h"
#include "error.h"

/// Verify all contract pre/post conditions in the AST using Z3.
/// Returns 0 if all contracts verify, 1 if any fail, -1 on error.
/// If Z3 is not linked, returns 0 silently.
int z3_verify_contracts(AstNode* root, ErrorList* errors, int print_output);

#endif