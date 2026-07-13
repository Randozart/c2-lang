# Phase B — Type Checker

**Date:** 2026-07-13

## Scope
Implement a type system and type-checking pass for C². Each AST node gets a
`Type* type` field (already declared in `ast.h`). The type checker walks the
AST and infers/validates types for all expressions and statements. It also
validates contract arity, derivation example types, `result` usage, and
ownership modifiers.

## Files Touched
- **New:** `src/type.h` — Type enum, struct Type, construction/comparison API
- **New:** `src/type.c` — Type creation, equality, formatting, cleanup
- **New:** `src/typecheck.h` — Public type-checker API (single entry point)
- **New:** `src/typecheck.c` — Type checker pass (AST visitor)
- **Modified:** `src/main.c` — Run typecheck after parse in `build` and `check`
- **Modified:** `Makefile` — No changes needed (wildcard picks up new .c files)

## Architecture Impact
Type checking is a new pipeline stage between parsing and codegen. The
`typecheck_source()` function annotates the AST with `Type*` structs and
reports type errors via the existing `ErrorList`. Errors are non-fatal
(accumulate, continue checking).

## Type System Design
- `TypeKind` enum: `VOID, BOOL, INT8..64, UINT8..64, FLOAT, DOUBLE,
  POINTER, ARRAY, FUNCTION, STRUCT, UNION, ENUM, NAMED, INVALID`
- `struct Type`: kind, int width/signed, subtype ptr, param types,
  array size, name, struct Symbol*
- Heap-allocated with refcount-ish ownership (one owner per node; freed by
  `ast_free_tree`)

## Type Checker Pass
1. First pass: collect all function/struct/union/enum declarations and
   typedefs into a type environment / symbol table.
2. Second pass: walk function bodies post-order, infer/validate types.
3. Validation rules:
   - Binary arithmetic: both operands must be arithmetic, result is usual
     arithmetic conversion
   - Binary comparison: both operands must be arithmetic or pointers
   - Assignment: dest type must be assignable from src type
   - Function call: args must match param types (exact or assignable)
   - Return: expr type must match declared return type
   - Contract: pre/post must be boolean expressions
   - Derivation: example inputs must match param count/types
   - `result` only valid in postcondition of non-void function
   - Ownership (`borrow`/`own`) only on pointer params
   - `when` condition must be boolean

## Verification
- Run `make test` — all existing tests must pass
- New test file: `tests/typecheck/test_typecheck.c` with unit tests for
  each validation rule
