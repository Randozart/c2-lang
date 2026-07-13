// 2026-07-13 — Program synthesis engine for C².
//   Cost-guided enumerative search for derivation blocks.
//   Supports hard/soft constraints, Pareto frontier, source insertion.

#ifndef C2_DERIVE_H
#define C2_DERIVE_H

#include "ast.h"
#include "error.h"

/// Run the synthesis engine for all functions with derivation blocks
/// that have no body (synthesis spec). Prints Pareto frontier and
/// optionally mutates the source file.
/// Returns 0 on success (or no work to do), 1 on synthesis failure.
int derive_synthesize(AstNode* root, const char* source_path,
                      ErrorList* errors, int interactive);

#endif