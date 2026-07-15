# Remaining Gaps — Closeout Plan

**Date:** 2026-07-14
**Status:** Plan
**Scope:** Close all remaining implementation gaps that are not deliberate design choices.
**Deliberately excluded (not gaps):** `if`/`else`/`goto` (use `when`), loop invariants, compound/designated initializers, recursive derivation, auto-budget, field-level state tracking, float BV limitations, synthesis engine enhancements (SMT-guided, Pareto auto-tuning).

---

## Phase 1: Self-Hosting Blockers

These prevent the compiler from compiling its own source.

### 1.1 — `static`/`const` qualifier emission

**Files:** `src/parser.c`, `src/codegen.c`
**Status:** Parser consumes `static`/`const`/`extern` in `parse_type` while-loop but discards them — last-token-wins, no storage.

**Fix (parser):** After the type-token while-loop in `parse_type`, check if any consumed token was `TOK_STATIC` or `TOK_CONST`. Set a flag on the returned NODE_VARIABLE (use existing `AstNode.flags` bits, e.g., `flags |= 1` for static, `flags |= 2` for const). Similarly in the NODE_DECL path at `generic_decl:`, propagate these flags.

**Fix (codegen):** In the NODE_DECL case and the function emission path, check `flags` bits and emit `static`/`const` before the type.

**Test:** Write a parser test `parse_static_var`, `parse_const_var` and a codegen test `emit_static_global`, `emit_const_param`.

---

### 1.2 — `extern` declaration support

**Files:** `src/parser.c`, `src/codegen.c`, `include/c2.h`
**Status:** `NODE_EXTERN` exists as a dead AST node kind — never created by parser, never emitted by codegen. Parser discards `extern` in `parse_type`.

**Fix (parser):** In `generic_decl:`, detect `TOK_EXTERN` before parsing the type. Create a `NODE_EXTERN` node wrapping the following declaration (or function signature without body). The `extern` keyword should skip the contract requirement for functions.

**Fix (codegen):** Add `case NODE_EXTERN:` that emits `extern` followed by the child declaration, then `;`.

**Test:** Write a parser test `parse_extern_func` and codegen test `emit_extern`.

**Note:** Library bindings (P2.1–P2.6) depend on this — they need `extern` declarations in `c2.h`.

---

### 1.3 — Variadic `...` parsing

**Files:** `src/lexer.c`, `src/lexer.h`, `src/parser.c`
**Status:** `...` is tokenized as `TOK_DOT` — indistinguishable from `.`. Parser has no variadic parameter handling.

**Fix (lexer):** Add `TOK_ELLIPSIS` to `TokenKind` enum. In the `...` detection code (three consecutive dots), emit `TOK_ELLIPSIS` instead of `TOK_DOT`.

**Fix (parser):** In `parse_parameter_list`, after parsing normal params, if `TOK_ELLIPSIS` is seen, add a child with kind `NODE_ELLIPSIS` (or reuse the param mechanism). After the `...`, `)` must follow. Skip typechecking for the ellipsis.

**Fix (typecheck):** In function type building, when the last param is `NODE_ELLIPSIS`, mark the function type as variadic (add a flag to `Type` struct or set `param_count` accordingly and omit the ellipsis from param types).

**Fix (codegen):** In function parameter emission, after all normal params, emit `, ...` if variadic.

**Test:** Lexer test for `TOK_ELLIPSIS`, parser test for variadic parameter list, codegen test for `printf(const char*, ...)`.

---

### 1.4 — `void*` full support

**Files:** `src/typecheck.c`, `src/type.c`, tests
**Status:** `type_assignable` already allows `void* ↔ T*`. `type_from_tok` recognizes `"void"`. `void*` can be declared as a pointer type. The gap is that `void*` is not tested end-to-end and may have edge cases in codegen or typechecking of dereferences/arithmetic.

**Fix:** Write an end-to-end test that declares `void* ptr = malloc(100);`, casts it, and frees it (once `extern` is working). Verify codegen emits `void*` correctly. Ensure `*ptr` (deref of void*) produces a meaningful error (can't deref void*).

**Note:** Low-code-change item — mostly testing and edge-case hardening. Depends on 1.2 (extern) for malloc/free declarations.

---

### 1.5 — Struct member typechecking

**Files:** `src/typecheck.c`, `src/type.h`, `src/type.c`
**Status:** `NODE_MEMBER` and `NODE_DEREF` are stubbed with `node->type = TYPE_INVALID`. Struct field types are not recursively typechecked.

**Fix (typecheck struct decl):** In the `NODE_STRUCT_DECL` handler, iterate over `NODE_STRUCT_FIELD` children. For each field, parse its type and register the field name + type in a per-struct symbol table (or populate `struct_sym` on the struct's Type).

**Fix (NODE_MEMBER):** Look up the member name in the struct's field table. If found, set `node->type` to the field's type. If not found, error "no member named '...'".

**Fix (NODE_DEREF):** Same as NODE_MEMBER, but first dereference the pointer type.

**Test:** `struct Point { int32_t x; int32_t y; };` — test `p.x` type resolution, `p.z` error, pointer member access `ptr->x`.

---

### 1.6 — Drop injection position

**Files:** `src/drop.c`
**Status:** Function-scope drop calls are added as children of `NODE_FUNCTION` (line 120) instead of inside the `NODE_BLOCK` body. The codegen emits them after the function's closing `}`, producing invalid C.

**Fix:** In the NODE_FUNCTION handler, instead of `ast_add_child(node, dc)` (adds to function node), insert the drop call at the END of the block: `ast_add_child(body, dc)`. This ensures `free(ptr); ptr = NULL;` appears before the block's closing `}`.

**Test:** Add a drop injection test that verifies function-scope owned pointers get their drop calls inside the block, not at the function level. Compile the result with gcc to confirm valid C.

---

## Phase 2: Correctness

### 2.1 — Verifier ITE chain placeholder

**Files:** `src/verifier.c`
**Status:** Postcondition body modeling uses a placeholder at line 310 instead of building proper when-guard chains.

**Fix:** Replace the placeholder with actual ITE (if-then-else) chain construction from the function body's when-guard structure. This requires walking the body AST, extracting when-guard conditions, and building nested Z3 ITE expressions representing the path-dependent return value.

**Test:** Add verifier tests for functions with multiple when-guard paths where the postcondition depends on which path is taken.

---

### 2.2 — Codegen unknown node warning

**Files:** `src/codegen.c`
**Status:** Default case in `emit_node` silently skips unknown node kinds.

**Fix:** Change the default case from `break` to emitting `/* unknown node kind %d */` comment so unhandled nodes are surfaced during development.

**Test:** A codegen test that passes an unsupported node and checks for the comment in output.

---

## Phase 3: Library Bindings

### 3.1 — `include/c2.h` expansion

**Files:** `include/c2.h`
**Status:** Currently has compatibility macros and convenience typedefs but no function declarations.

**Fix:** Add `extern` function declarations for:
- Memory: `malloc`, `calloc`, `realloc`, `free`
- I/O: `printf`, `fprintf`, `snprintf`, `sprintf`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`
- String: `strcmp`, `strncmp`, `strlen`, `strdup`, `strchr`, `strstr`
- Memory: `memcpy`, `memset`, `memcmp`, `memmove`
- Process: `exit`, `system`, `getenv`

Each declaration needs the `extern` keyword (depends on 1.2), correct return types, and variadic `...` where needed (depends on 1.3).

**Test:** Write a C² program that calls `printf`, `malloc`, `free`, `strlen` and compiles + runs via the full pipeline (transpile → gcc → run).

---

## Phase 4: Test/DX Improvements

### 4.1 — Test runner robustness

**Files:** `Makefile`
**Status:** Test binary crashes (assertion failure) don't print per-test summaries — the binary just aborts.

**Fix:** Wrap each test binary in a runner script that captures stderr and prints a summary count even on crash. Use `set -o pipefail` and `trap` to handle aborts gracefully.

### 4.2 — Fix `examples/swap_bytes.c2`

**Files:** `examples/swap_bytes.c2`
**Status:** Has a typecheck error with array indexing that prevents `make examples` from succeeding.

**Fix:** Either fix the array index expression to use correct syntax, or rewrite the example to not require array indexing until struct/array support matures.

### 4.3 — Fix `make examples` and `--emit-c` path

**Files:** `Makefile`, `src/main.c`
**Status:** `make examples` uses "SKIP (compiler not ready)". `--emit-c` output path fragile for paths without `.c2`.

**Fix:** Resolve P0 and P2 gaps so examples compile. Fix path handling in `main.c:113` to handle paths with multiple dots or no `.c2`.

---

## Implementation Order

```
Phase 1 (self-hosting blockers):
  ├── 1.6 — Drop injection position fix (quick, isolated bug)
  ├── 1.2 — Extern declaration support (unblocks P2)
  ├── 1.1 — static/const qualifier emission
  ├── 1.3 — Variadic ... parsing
  ├── 1.4 — void* full support (tests + edge cases)
  └── 1.5 — Struct member typechecking

Phase 2 (correctness):
  ├── 2.2 — Codegen unknown node warning (trivial)
  └── 2.1 — Verifier ITE chain

Phase 3 (library bindings):
  └── 3.1 — include/c2.h expansion

Phase 4 (test/DX):
  ├── 4.1 — Test runner robustness
  ├── 4.2 — Fix swap_bytes example
  └── 4.3 — Fix make examples + --emit-c path
```

After Phase 4, the compiler should be self-hosting capable: able to transpile its own source, compile it with gcc, and produce a working binary.

Estimated order of difficulty (easiest first): 1.6, 2.2, 1.2, 1.1, 1.3, 1.4, 3.1, 1.5, 4.1, 4.2, 4.3, 2.1.
