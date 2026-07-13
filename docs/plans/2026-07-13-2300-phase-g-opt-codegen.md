# Phase G: Optimizing Codegen

**DateTime:** 2026-07-13 2300
**Scope:** Enhance the C code emitter with optimization-friendly annotations
based on borrow analysis, VRP ranges, and contract proofs.

## Optimizations

### 1. `restrict` on `borrow` pointer parameters
`borrow int32_t* p` → `int32_t *restrict p`  
Tells the C compiler the pointer doesn't alias other parameters.

### 2. `const` on read-only borrow data
`borrow int32_t* p` → `const int32_t *restrict p`  
Pointee data is read-only when borrowed, enabling more C optimizations.

### 3. `__builtin_unreachable()` after VRP-proven branches
When VRP proves a when-guard condition, emit `__builtin_unreachable()`
in the else-path (the path after the guard) to help the C compiler's
optimizer eliminate dead code.

### Files
- `src/codegen.c` — Modified: emit restrict/const/unreachable annotations
- `tests/codegen/test_codegen.c` — New tests for optimization annotations

## Verification
- All existing tests pass
- New tests verify restrict/const/unreachable annotations in output
PLANEOF