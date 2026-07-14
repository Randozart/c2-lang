# Phase H.0: Self-Hosting Gap Fixes

**Date:** 2026-07-14
**Status:** Plan / Execution
**Duration estimate:** 1 session

## Scope

Fix the critical gaps in C²'s parser, codegen, and type system that would
prevent self-hosting. These are the missing features that the Phase H plan
either underestimated or missed entirely.

## Files Touched

| File | Change |
|------|--------|
| `src/parser.c` | Add cast expression parsing (`(type)expr` → NODE_CAST) |
| `src/codegen.c` | Add NODE_SIZEOF, NODE_DROP_CALL, NODE_PP_DEFINE, NODE_EXTERN emission; auto-include c2.h |
| `src/type.h` | Add `type_is_void_pointer` helper (optional) |
| `src/type.c` | Ensure void* assignability works for pointer-to-void (`void*`) |
| `include/c2.h` | Add extern stdlib/stdio/string declarations for self-hosting |

## Gap Analysis (from audit)

### Critical Gaps (must fix)

1. **Cast expressions `(type)expr` — parser silently drops them**
   - `parse_primary` handles `(expr)` as parenthesized expression, returning inner expr
   - Cast like `(int32_t)x` is parsed as `int32_t` (variable ref) then error on `)`
   - Need: detect type-cast pattern in `parse_unary` or `parse_primary`
   - Fix: In `parse_unary`, before unary ops, try to match `(type)expr` pattern

2. **`NODE_SIZEOF` — codegen silently skips it**
   - Parser creates NODE_SIZEOF correctly, typechecker skips it, codegen has no case
   - Fix: Add case in codegen that emits `sizeof(...)`

3. **`NODE_DROP_CALL` — codegen silently skips it**
   - Drop injection inserts NODE_DROP_CALL nodes which call free()
   - Fix: Add case in codegen that emits `free(name);`

4. **`NODE_PP_DEFINE` — codegen silently skips it**
   - Plan replaces enums with `#define` constants
   - Fix: Add case in codegen that emits `#define NAME VALUE`

5. **`NODE_EXTERN` — codegen silently skips it**
   - Node kind exists, parser creates for `extern` decls
   - Fix: Add case in codegen that emits `extern ...`

### Verified Working (no fix needed)

- **`NODE_MEMBER` / `NODE_DEREF`** — parser + codegen fully handle both
- **`NODE_UNARY_OP` for `&` and `*`** — typechecker + codegen handle correctly
- **`void*` assignability** — `type_assignable` already handles `void*` ↔ any pointer
- **`type_pointer` construction** — pointer-to-void can be built via `type_pointer(type_primitive(TYPE_VOID))`

## Verification

1. `make build` compiles successfully
2. `make test` passes all 180+ tests
3. Newly written C² test files compile through the pipeline
4. Cast expressions produce correct C output with `sizeof` preserved

## Implementation Order

1. Add cast expression parsing (parser.c)
2. Add NODE_SIZEOF handling (codegen.c)
3. Add NODE_DROP_CALL handling (codegen.c)
4. Add NODE_PP_DEFINE handling (codegen.c)
5. Add NODE_EXTERN handling (codegen.c)
6. Auto-include c2.h in codegen output
7. Build and test
