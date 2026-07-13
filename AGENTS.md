# C² (Contract Enforced C, Squared with Brackets) — Agent Guidelines

This document is the single source of truth for the `c2-lang` project. Every AI agent working on this codebase MUST read and obey these rules before writing any code.

## Anchored Summary

### Objective
Build a C-to-C semantic transpiler (Contract Enforced C) that extends standard C23 with compile-time verified contracts, automatic memory management via lexical borrow checking, and optional program synthesis. The compiler is written in pure C23 and targets self-hosting.

### Key Decisions
- **Implementation language:** Pure C23 (for self-hosting bootstrapping)
- **Output:** Standard C99/C23, compiled by GCC/Clang/TCC
- **Verification engine:** Z3 SMT solver via `z3.h` C API
- **Contract syntax:** `[pre][post]`, `[[post]` (double-open = no pre), `[pre]]` (double-close = no post)
- **Derivation:** `:= { input -> output; }` block — assertion when body present, synthesis spec when body absent
- **Memory management:** 5-state lexical variable state machine (`UNINITIALIZED → OWNED → BORROWED/MOVED → DROPPED`)
- **Build modes:** Driver mode (transpile + compile) by default; `--emit-c` for transpile-only

### Work State
- Specification document (`docs/spec.md`) is complete
- Project scaffolding is in place (`src/`, `include/`, `tests/`, `examples/`)
- First implementation phase (recursive-descent parser) has not started

### Next Move
Begin Phase A: Implement the recursive-descent parser, AST definitions, and C code generator.

---

## 1. Immutable Coding Standards

Every AI agent MUST follow these rules. They are not optional. Code that violates these rules must be rejected.

### Rule 1.1: Flat Control Flow — No Nesting
Maximum indentation depth is **2 levels**. Never write nested "arrowhead" code.

**Instead of:**
```c
void process(int x) {
    if (x > 0) {
        if (validate(x)) {
            result = transform(x);
            if (result != NULL) {
                *out = result;
                return;
            }
        }
    }
}
```

**Write:**
```c
void process(int x) {
    if (x <= 0) return;
    if (!validate(x)) return;
    result = transform(x);
    if (result == NULL) return;
    *out = result;
}
```

### Rule 1.2: Every Function Needs a Doc Comment
Every function, struct, typedef, and module must have a doc comment explaining its intent and, for functions, its parameters and return value.

```c
/// Parse a contract block and return the precondition and postcondition AST nodes.
/// `expr_start` points to the opening bracket. Returns 0 on success, -1 on error.
int parse_contract(Lexer* lex, AstNode** pre, AstNode** post);
```

### Rule 1.3: No Placeholder Code
Never write `// TODO`, `XXX`, `...`, or stub implementations. Every code path must be fully implemented. If a feature is not yet built, do not write code for it at all.

### Rule 1.4: Assertions and Defensive Checks
Every module MUST validate its inputs and internal state:
- Check pointer parameters for NULL at function entry (unless proven otherwise).
- Assert array bounds before indexing.
- Print diagnostic messages for unexpected states.
- Use `assert()` for internal invariants that should never fail.

### Rule 1.5: Early Returns Over Else-If
Functions with more than two branches must use guard clauses and early returns. The `else if` chain is forbidden beyond a single `if/else`.

### Rule 1.6: `const` Correctness
Mark every parameter and variable as `const` when the value is not mutated. Functions that accept read-only pointers MUST use `const` in the generated C output.

### Rule 1.7: No Global Mutable State
All compiler state (symbol tables, scope stacks, error lists) must be passed through explicit context structs. No file-scope mutable variables.

---

## 2. Workflow Rules

### Rule 2.1: Plan Before Code
Before writing ANY code, create a plan file at:
```
docs/plans/YYYY-MM-DD-HHMM-<short-description>.md
```
The plan must contain:
- **DateTime stamp** in the filename and at the top of the file.
- **Scope**: what is being built/changed and why.
- **Files touched**: every file that will be created or modified.
- **Architecture impact**: does this change the AST, the pipeline, or the CLI?
- **Verification**: how will you confirm it works.

### Rule 2.2: Architecture Documentation
Every architectural change must be recorded in:
```
docs/architecture/YYYY-MM-DD-HHMM-<short-description>.md
```

### Rule 2.3: Change Annotations
Every modified file gets a header comment at the very top:

```c
// 2026-07-13 — Added recursive-descent parser for C² contracts.
//   Parses [pre][post], [[post], [pre]] and derivation blocks.
```

### Rule 2.4: Continuous Git Commits
Commit **after every single completed feature step** — never batch multiple logically separate changes into one commit.

Before every commit:
1. Verify the project compiles: `make build` (or `gcc -Wall -Wextra src/*.c -o build/c2 -lz3`)
2. Run the test suite: `make test` or `make test-all`
3. Run linting if available

Commit format:
```
phase: one-line summary [plan: docs/plans/YYYY-MM-DD-HHMM-foo.md]
```

---

## 3. Testing Standards

### Rule 3.1: Tests Inline
Every C source module must have a `#ifdef TEST` section or associated test file in `tests/`. Test files follow the naming convention `tests/<module>/test_<feature>.c`.

### Rule 3.2: Test What You Build
Each phase must include:
- **Unit tests** for each function in the module.
- **Integration tests** for the phase boundary.
- **Regression tests** for any bugs found during development.

### Rule 3.3: No Test Placeholders
Test files must contain real, runnable tests. Never write `// TODO: add tests` or skeleton test functions.

---

## 4. Project Structure

```
c2-lang/
├── AGENTS.md                        # This file
├── Makefile                         # Build orchestration
├── README.md                        # Project overview
├── docs/
│   ├── spec.md                      # Full language specification
│   ├── plans/                       # Implementation plans (Rule 2.1)
│   └── architecture/                # Architecture docs (Rule 2.2)
├── include/
│   └── c2.h                         # Standard library / compatibility header
├── src/
│   ├── main.c                       # CLI entry point
│   ├── lexer.c / lexer.h            # Tokenizer
│   ├── parser.c / parser.h          # Recursive-descent parser
│   ├── ast.h                        # AST node definitions
│   ├── symbol.h                     # Symbol table + scope stack
│   ├── state.h                      # Variable state machine
│   ├── type.c / type.h              # Type system
│   ├── vrp.c / vrp.h                # Value range propagation
│   ├── verifier.c / verifier.h      # Z3 SMT integration
│   ├── borrow.c / borrow.h          # Borrow checker
│   ├── drop.c / drop.h              # Drop injection pass
│   ├── codegen.c / codegen.h        # C pretty-printer
│   ├── derive.c / derive.h          # Program synthesis engine
│   ├── driver.c / driver.h          # GCC/Clang invocation
│   └── error.h                      # Error reporting
├── tests/
│   ├── lexer/                       # Lexer unit tests
│   ├── parser/                      # Parser tests
│   ├── verifier/                    # Z3 integration tests
│   ├── borrow/                      # Borrow checker tests
│   ├── codegen/                     # Codegen output comparison tests
│   └── integration/                 # End-to-end tests
└── examples/
    └── swap_bytes.c2                # Example C² program
```

---

## 5. Module Summaries

### `src/lexer.c`
Tokenizes C² source code into a stream of tokens. Recognizes C23 tokens plus C²-specific tokens: `borrow`, `own`, `when`, `no_derive`, `:=`, and contract brackets (`[`, `]]`, `[[`).

### `src/parser.c`
Recursive-descent parser consuming tokens from the lexer and producing an AST. Parses contracts, guards, derivation blocks, and ownership modifiers in addition to standard C syntax.

### `src/ast.h`
AST node type definitions. Contains `AstNode` struct with kind, children, token location, type annotations, and VRP range metadata.

### `src/symbol.h`
Symbol table implementation with scope stack. Each `Symbol` stores name, type, variable state (`UNINITIALIZED` through `DROPPED`), borrow count, and a reference to the drop function if applicable.

### `src/state.h`
The 5-state variable lifecycle enum and transition validation functions.

### `src/verifier.c`
Z3 SMT solver integration. Translates AST contract expressions into Z3 bit-vector formulas and runs proof queries. Also performs falsifiability checks.

### `src/vrp.c`
Value range propagation pass. Infers variable ranges from loop boundaries and branch conditions.

### `src/borrow.c`
Lexical borrow checker implementing the state machine rules. Walks the AST and rejects illegal ownership transitions.

### `src/drop.c`
Drop injection pass. Mutates the AST by inserting destructor calls before scope boundaries.

### `src/codegen.c`
C pretty-printer that converts the verified, mutated AST into standard C source. Selectively emits `restrict`, `__builtin_unreachable()`, and `__attribute__((nonnull))` based on Z3 proof results.

### `src/derive.c`
Program synthesis engine. Supports SMT-based component synthesis (Z3) and depth-bounded enumerative search fallback. Handles surgical source-code insertion.

### `src/driver.c`
Invokes the system C compiler (GCC, Clang, or TinyCC) on the emitted C file to produce the final binary.

### `src/main.c`
CLI entry point. Parses command-line flags and dispatches to the appropriate pipeline phase (build, check, derive).

---

## 6. Technology Stack

- **C23** — Implementation language (compiler written in pure C)
- **Z3** — SMT solver via `z3.h` C API
- **GCC 13+ / Clang 18+** — Downstream C compilers (invoked in driver mode)
- **Make** — Build orchestration
- **Criterion** or custom test harness — Unit testing

---

## 7. Current Implementation Status

| Phase | Module | Status |
|-------|--------|--------|
| A | Lexer | Not started |
| A | Parser | Not started |
| A | AST | Not started |
| A | Codegen (basic) | Not started |
| B | Type checker | Not started |
| C | Z3 verifier | Not started |
| D | VRP | Not started |
| E | Borrow checker | Not started |
| E | Drop injection | Not started |
| F | Derivation/synthesis | Not started |
| G | Optimizing codegen | Not started |
| H | Self-hosting | Not started |
