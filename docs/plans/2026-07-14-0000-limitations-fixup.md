# Limitations Fixup Plan

**Date:** 2026-07-14
**Status:** Plan
**Scope:** Address all known limitations, stubs, and future enhancements
          documented across the codebase.

---

## Priority Tiers

### P0 — Self-Hosting Blockers (must fix before Phase H)

| # | Issue | Location | Fix |
|---|-------|----------|-----|
| P0.1 | **`void*` not supported** | `src/typecheck.c`, `src/parser.c` | Add `void*` pointer rules: `void*` assignable from/to any pointer; codegen emits `void*` correctly |
| P0.2 | **`static` qualifier not emitted** | `src/parser.c`, `src/codegen.c` | Parser already consumes `static`; codegen must emit `static` for file-scope functions/vars |
| P0.3 | **Variadic `...` not parsed** | `src/parser.c` | Allow `...` token in parameter lists; skip typechecking; codegen emits `...` |
| P0.4 | **`const` qualifier stripped** | `src/parser.c` | Parser consumes `const` but discards it — store const flag on NODE_DECL and emit in codegen |
| P0.5 | **Struct member access typed TYPE_INVALID** | `src/typecheck.c:686-696` | NODE_MEMBER and NODE_DEREF need basic type propagation from struct field types |
| P0.6 | **`extern` declarations unverified** | `src/codegen.c` | Add codegen test for NODE_EXTERN with complex types (function pointers, nested pointers) |

### P1 — Correctness (contracts, verifier, borrow checker)

| # | Issue | Location | Fix |
|---|-------|----------|-----|
| P1.1 | **Contracts with loops skipped** | `src/verifier.c` | Skip gracefully (already done) — document as known limitation; future loop invariants |
| P1.2 | **Float BV semantics (no NaN/Inf)** | `src/verifier.c` | Document as known; Z3 BV can't model IEEE 754 special values easily |
| P1.3 | **Verifier ITE chain placeholder** | `src/verifier.c:310` | Replace placeholder with working when-guard chain building for postcondition body model |
| P1.4 | **Drop injection appends to function node** | `src/drop.c:120` | Insert NODE_DROP_CALL at correct position in block (before `}`), not appended to function |
| P1.5 | **Derivation `:= {...} {body}` syntax unsupported** | `src/parser.c:228-236` | After parsing derivation block, if next token is `{`, parse it as function body and attach |
| P1.6 | **Codegen default case skips unknown nodes silently** | `src/codegen.c:882` | Emit a comment or warning for unhandled node kinds |

### P2 — Missing Library Bindings (needed for self-hosting)

| # | Issue | Fix |
|---|-------|-----|
| P2.1 | **`malloc`/`calloc`/`realloc`/`free`** | Add extern declarations to `include/c2.h` |
| P2.2 | **`printf`/`fprintf`/`snprintf`/`sprintf`** | Add extern declarations with `...` to `include/c2.h` |
| P2.3 | **`fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell`** | Add extern declarations to `include/c2.h` |
| P2.4 | **`strcmp`/`strncmp`/`strlen`/`strdup`/`strchr`/`strstr`** | Add extern declarations to `include/c2.h` |
| P2.5 | **`memcpy`/`memset`/`memcmp`/`memmove`** | Add extern declarations to `include/c2.h` |
| P2.6 | **`exit`/`system`/`getenv`** | Add extern declarations to `include/c2.h` |

### P3 — Test & DX Improvements

| # | Issue | Location | Fix |
|---|-------|----------|-----|
| P3.1 | **Test suite crashes don't print per-test counts** | `Makefile` | Wrap test binary in a runner that prints summary even on crash |
| P3.2 | **VRP for-loop test silently skips** | `tests/vrp/test_vrp.c:101` | Use a working for-loop pattern that parses correctly |
| P3.3 | **`make examples` uses "SKIP (compiler not ready)"** | `Makefile:86` | Fix the underlying issues so examples build without skipping |
| P3.4 | **Swap_bytes example has typecheck error** | `examples/swap_bytes.c2:26` | Fix array indexing or example to not require it |
| P3.5 | **`--emit-c` output path fragile** | `src/main.c:113` | Handle paths without `.c2` extension, paths with multiple dots |

### P4 — Future Enhancements (post-self-hosting)

| # | Enhancement | Notes |
|---|-------------|-------|
| P4.1 | **Loop invariant inference** | Infer loop invariants from for-loop bounds and VRP ranges |
| P4.2 | **SMT-guided synthesis** | Use Z3 component-based synthesis instead of brute-force enumeration |
| P4.3 | **Recursive derivation** | Detect numeric patterns and synthesize closed forms |
| P4.4 | **Auto-budget for synthesis** | Binary-search for minimum budget that satisfies hard examples |
| P4.5 | **Field-level state tracking** | Track per-field ownership in struct symbols for safe early-free |
| P4.6 | **Compound literals** | Support C99 compound literal syntax |
| P4.7 | **Designated initializers** | Support C99 designated initializer syntax |

---

## Implementation Order

```
Phase 1: P0 items (self-hosting blockers)
  ├── P0.1 void* support
  ├── P0.2 static qualifier
  ├── P0.3 variadic ... parsing
  ├── P0.4 const qualifier preservation
  ├── P0.5 struct member typechecking
  └── P0.6 extern codegen test

Phase 2: P1 items (correctness)
  ├── P1.3 verifier ITE chain
  ├── P1.4 drop injection position
  ├── P1.5 derivation + body syntax
  └── P1.6 codegen unknown node warning

Phase 3: P2 items (library bindings)
  ├── P2.1–P2.6 include/c2.h expansion
  └── Test each binding with a C² program

Phase 4: P3 items (test/DX)
  ├── P3.1 test runner robustness
  ├── P3.2–P3.5 fix fragile tests and examples
  └── Verify 180+ tests still pass after each change
```
