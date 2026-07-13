# C² Compiler Pipeline Architecture

**Date:** 2026-07-13
**Status:** Current (Phases A–G complete)

---

## 1. Overview

The `c2c` compiler is a **9-pass semantic transpiler** from C² to standard C23.
Each pass consumes the AST and either annotates it (type checking, VRP, borrow)
or mutates it (drop injection) before the final codegen pass emits C source.

```
Source (.c2)
  │
  ├── Pass 1: Lexer   ─────────────────── Token stream
  ├── Pass 2: Parser  ─────────────────── AST (raw)
  ├── Pass 3: Typecheck ───────────────── AST (with type annotations)
  ├── Pass 4: VRP     ─────────────────── AST (with value ranges)
  ├── Pass 5: Borrow  ─────────────────── AST (with validated states)
  ├── Pass 6: Drop    ─────────────────── AST (with NODE_DROP_CALL inserted)
  ├── Pass 7: Codegen ─────────────────── C source text
  ├── Pass 8: Z3 Verify ───────────────── Proof results (advisory, no AST change)
  └── Pass 9: Synthesis ───────────────── Source file mutation (c2c derive only)
```

---

## 2. Pass Details

### Pass 1: Lexer (`src/lexer.c`, `src/lexer.h`)

Tokenizes C² source into a stream of tokens. Recognizes all C23 tokens plus
C²-specific tokens: `borrow`, `own`, `when`, `no_derive`, `:=`, and contract
brackets (`[`, `]]`, `[[`).

**Key design:**
- `lexer_scan()` advances to the next token, stores it in `current_tok`
- `lexer_peek()` returns a copy of what the next `lexer_scan()` would return
  (saves/restores lexer state)
- `lexer_advance()` returns `current_tok` and calls `lexer_scan()`
- Token locations track `(line, col)` — column is the *end* column after
  the last consumed character

### Pass 2: Parser (`src/parser.c`, `src/parser.h`)

Recursive-descent parser consuming tokens from the lexer and producing an AST.
Parses contracts, guards, derivation blocks, and ownership modifiers.

**Key design:**
- `parse_declaration()` handles top-level declarations (functions, globals, typedefs)
- Contracts are parsed BEFORE the function signature: `[pre][post]` → attached
  as `NODE_CONTRACT_PRE` / `NODE_CONTRACT_POST` wrapper children of `NODE_FUNCTION`
- `when` guards are parsed as `NODE_WHEN` with condition child[0] and statement child[1]
- `borrow`/`own` qualifiers add `NODE_BORROW_PARAM` / `NODE_OWN_PARAM` children
  to the parameter `NODE_DECL`
- Derivation blocks `:= { ... }` are parsed as `NODE_DERIVATION` children
- `if` and `goto` produce targeted error messages (not C² constructs)
- `synchronize()` error recovery: skips tokens until `;` or `}` (does NOT
  consume `}` — that belongs to the enclosing scope)

**AST child layout for a function:**
```
NODE_FUNCTION "f"
  ├── NODE_VARIABLE "int32_t"       (return type)
  ├── NODE_CONTRACT_PRE             (precondition wrapper)
  │   └── NODE_BINARY_OP ">"        (expression)
  ├── NODE_CONTRACT_POST            (postcondition wrapper)
  │   └── NODE_BINARY_OP "=="       (expression)
  ├── NODE_PARAM_LIST
  │   └── NODE_DECL "x"
  ├── NODE_BLOCK                    (body)
  │   └── NODE_RETURN / NODE_WHEN / etc.
  └── NODE_DERIVATION               (derivation block, optional)
      └── NODE_DERIV_EXAMPLE
```

### Pass 3: Type Checker (`src/typecheck.c`, `src/typecheck.h`)

Walks the AST post-order and populates `node->type` with the inferred Type*.
Validates type compatibility for assignments, function calls, return values,
contracts, derivation examples, and borrow/own pointer checks.

**Key design:**
- Post-order walk: children are typechecked before their parent
- For functions: the body and contract children are SKIPPED during initial
  recursion (params aren't in scope yet). After the function scope is pushed
  and params are registered, the body and contracts are re-typechecked.
- `result` in postconditions resolves to the function's return type
- `current_return_type` is set during function body processing
- Types are deep-copied when assigned to AST nodes to prevent double-free
  during cleanup (`type_deep_copy()` in `src/type.c`)
- Each node that receives its own type pointer is the sole owner — no sharing
  between nodes or between nodes and the symbol table

**Type system (`src/type.h`, `src/type.c`):**
- `TypeKind` enum: `TYPE_VOID`, `TYPE_BOOL`, `TYPE_INT{8,16,32,64}`,
  `TYPE_UINT{8,16,32,64}`, `TYPE_FLOAT`, `TYPE_DOUBLE`, `TYPE_POINTER`,
  `TYPE_ARRAY`, `TYPE_FUNCTION`, `TYPE_STRUCT`, `TYPE_UNION`, `TYPE_ENUM`,
  `TYPE_NAMED`, `TYPE_INVALID`
- `Type` struct: `kind`, `is_signed`, `bit_width`, `subtype` (element/return),
  `param_types` (function params), `param_count`, `array_size`, `name`,
  `struct_sym`
- Constructors: `type_primitive()`, `type_pointer()`, `type_array()`,
  `type_function()`, `type_named()`, `type_deep_copy()`
- Queries: `type_is_integer()`, `type_is_floating()`, `type_is_arithmetic()`,
  `type_is_scalar()`, `type_is_pointer()`, `type_is_void()`, `type_is_error()`,
  `type_is_bool()`, `type_is_signed()`, `type_is_unsigned()`, `type_sizeof()`
- Comparison: `type_equal()`, `type_assignable()` (with integer promotion,
  float conversion, array decay, void* assignability)

### Pass 4: VRP (`src/vrp.c`, `src/vrp.h`)

Value Range Propagation infers integer variable ranges from assignments,
for-loop headers, and when-guard conditions. Populates `node->range` on
variable nodes and `sym->range` on symbol table entries.

**Inferred ranges:**
- Literal assignment: `x = 42` → `x ∈ [42, 42]`
- For-loop bounds: `for (i = 0; i < N; i++)` → `i ∈ [0, N-1]` in body
- When-guard refinement: `when x > 0` → `x ∈ [1, INT_MAX]` in guarded block
- Binary operations: addition, subtraction, multiplication (range of extremes)
- Ternary: union of then/else ranges
- Unresolved variables: full type range [type_min, type_max]

**Symbol table usage:**
- VRP reads/writes `sym->range` during its walk
- For when guards: saves the symbol's range, sets the refined range, walks
  the guarded block, then restores the original range

### Pass 5: Borrow Checker (`src/borrow.c`, `src/borrow.h`)

Validates variable state transitions using the 5-state machine
from `src/state.h`. Walks each function body and applies state transitions
based on variable access patterns.

**State machine:** `UNINITIALIZED → OWNED → BORROWED/MOVED → DROPPED`
- `STATE_UNINITIALIZED`: default for declared variables
- `STATE_OWNED`: after write/init — variable owns its resource
- `STATE_BORROWED`: after borrow — read-only reference exists
- `STATE_MOVED`: after ownership transfer
- `STATE_DROPPED`: after free/scope exit — resource released

**Actions and transitions:**
| Action | Transition | Error |
|--------|-----------|-------|
| READ on UNINITIALIZED | — | `uninitialized-read` |
| WRITE on UNINITIALIZED | → OWNED | OK |
| READ on OWNED | stays OWNED | OK |
| WRITE on OWNED | stays OWNED | OK |
| MOVE on OWNED | → MOVED | OK |
| DROP on OWNED | → DROPPED | OK |
| BORROW on OWNED | → BORROWED | OK |
| BORROW_END on BORROWED | → OWNED | OK |
| WRITE on BORROWED | — | `mutate-while-borrowed` |
| READ on MOVED | — | `use-after-move` |
| DROP on DROPPED | — | `double-drop` |

**Key design:**
- Walk order: post-order (children first), same as typechecker
- `free()` calls are detected by name and trigger DROP transition on the argument
- Parameters start in `STATE_OWNED` (set by typechecker)
- Symbol lookup uses `symtab_lookup()` (not `symtab_lookup_current()`) because
  function scopes are popped after typechecking (kept alive as "dead scopes")

### Pass 6: Drop Injection (`src/drop.c`, `src/drop.h`)

Inserts `NODE_DROP_CALL` nodes for variables still in `STATE_OWNED` at scope
boundaries. Skips variables already in `STATE_DROPPED` (early free).

**Key design:**
- Tracks owned variables in a `OwnedList` (dynamic array of Symbol*)
- When entering a block, records the starting count
- When exiting a block, checks each newly-owned variable's state
- If still `STATE_OWNED`, creates a `NODE_DROP_CALL` and appends it to the block
- At function level, appends drop calls to the function node (before derivation)

### Pass 7: Codegen (`src/codegen.c`, `src/codegen.h`)

C pretty-printer that converts the verified, mutated AST into standard C source.

**C² → C mappings:**
| C² | C |
|----|---|
| `when cond -> stmt` | `if (cond) { stmt }` |
| `when cond { ... }` | `if (cond) { ... }` |
| `borrow T* p` | `const T *restrict p` |
| `own T* p` | `T *restrict p` |
| `[pre][post] contracts` | Stripped (comments only) |
| `:= { ... }` derivation | Emitted as `// Derivation example:` comments |
| `NODE_DROP_CALL` | Stripped (drop is implicit in C — scope handles it) |

**Optimizations:**
- `restrict` on borrow/own pointer parameters (non-aliasing guarantee)
- `const` on borrow pointees (read-only guarantee)
- `__builtin_unreachable()` after VRP-proven when guards
- `#include <stdint.h>` automatically emitted at top of output

### Pass 8: Z3 Verifier (`src/verifier.c`, `src/verifier.h`)

Translates contract expressions into Z3 bit-vector formulas and runs proof
queries. Non-blocking — failures are reported but don't stop compilation.

**Proof strategy:**
1. Precondition satisfiability: assert pre, check SAT (must not be trivially false)
2. Postcondition validity: assert pre + body model, assert NOT(post), check UNSAT
   (no input can make pre true and post false)

**Expression translation (AST → Z3 BV):**
- Integer/float literals → `Z3_mk_(un)signed_int64()`
- Variables → symbolic constants with bit-vector sorts (8/16/32/64 bit)
- `result` → symbolic constant in postconditions
- Arithmetic: `bvadd`, `bvsub`, `bvmul`, `bvsdiv`
- Comparison: `bvslt`, `bvsgt`, `eq`, etc. with zero-extension to target width
- Logical: `and`, `or`, `not` (array form for Z3 API)
- Ternary: `ite`
- Cast: `sign_ext` / `extract`
- When-guard chains: `ite` (if-then-else) for branch modeling

**Z3 error handling:** Custom error handler prevents Z3's default `abort()`.

### Pass 9: Synthesis (`src/derive.c`, `src/derive.h`)

Cost-guided enumerative expression search for derivation blocks without bodies.
Generates expression trees up to a budget (default 7 ops), evaluates on
examples, and returns the Pareto frontier of (ops, penalty) trade-offs.

**Expression types:**
- `EXPR_CONST_INT`, `EXPR_CONST_FLOAT` — literals
- `EXPR_VAR` — parameter reference
- `EXPR_UNARY` — `-`, `~`
- `EXPR_BINARY` — `+`, `-`, `*`, `/`, `&`, `|`, `^`, `<<`, `>>`

**Cost model:**
- Hard constraints (tolerance=0): exact match required
- Soft constraints (tolerance>0): penalty = max(0, |actual - expected| - tol) / tol
- Total cost = op_count + sum(penalties)

**Pareto frontier:**
- Tracks (ops, penalty) pairs where no candidate dominates another
- Knee = closest to origin in (ops, penalty) space

**Source mutation:** Inserts synthesized `return EXPR;\n}` before the `:=`
derivation block. The derivation stays as an assertion for future builds.

---

## 3. Symbol Table (`src/symbol.h`, `src/symbol.c`)

Hash-table-based scope stack supporting dead-scope lookups.

**Scope structure:**
- Linked list of scopes, each with a hash table of symbols
- `symtab_push_scope()` creates a new scope (pushed on top)
- `symtab_pop_scope()` moves the current scope to the dead list (not freed —
  symbols are kept alive for node->symbol references)
- `symtab_destroy()` frees all scopes (active + dead + global)

**Dead scopes:**
When a function scope is popped after typechecking, the scope is moved to
`st->dead_scopes` rather than being freed. This keeps `node->symbol` pointers
valid for downstream passes (borrow checker, drop injection).
`symtab_lookup()` searches both active and dead scopes.

---

## 4. Variable State Machine (`src/state.h`, `src/state.c`)

5-state lifecycle with validated transitions:

```
UNINITIALIZED ──write──→ OWNED ──read──→ OWNED
                    │       │──write──→ OWNED
                    │       │──move──→ MOVED ──write──→ OWNED
                    │       │──drop──→ DROPPED
                    │       │──borrow──→ BORROWED ──borrow_end──→ OWNED
                    │       │              │──read──→ BORROWED
                    │       │              │──write──→ (error)
                    │       │              │──move──→ (error)
                    │       │              │──drop──→ (error)
                    │       │──borrow (with count)──→ BORROWED
                    │
                    └──borrow──→ BORROWED
```

State transitions are validated by `state_transition()` which takes
the current state, action, and borrow count. Returns `TRANS_OK` or an
error code (`TRANS_ERROR_USE_AFTER_MOVE`, etc.).

---

## 5. Test Architecture

Tests are standalone C programs in `tests/<module>/test_<module>.c`.
Each is compiled and run by `make test`:

```makefile
for test_src in $(TEST_SRCS); do
    gcc ... -o test_bin test_src $(TEST_OBJS) $(LDFLAGS)
    test_bin
done
```

`TEST_OBJS` excludes `main.o` (test binaries provide their own `main()`).
Z3 is linked via `$(LDFLAGS)` (from `pkg-config --libs z3`).

Integration tests in `tests/integration/test_full_pipeline.c` compile
C² source files all the way through to binary execution, verifying that
the output matches expected exit codes.

**Test counts:** 180+ tests across 10 suites.
