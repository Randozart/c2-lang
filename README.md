# C² (Contract Enforced C, Squared with Brackets)

**Compile-time verified contracts, automatic memory management, and program synthesis for C.**

C² is a C-to-C semantic transpiler (the `c2c` compiler) that extends standard C23
with mathematically proven safety guarantees — without runtime overhead, garbage
collection, or sacrificing C ABIs.

```
[c2 source]  →  c2c  →  Z3 SMT Verifier  →  Borrow Checker  →  VRP  →  Codegen  →  GCC/Clang
                             ↓                      ↓
                     Contract proofs          Drop injection
```

## Pipeline Architecture

The compiler runs up to **9 passes** in sequence:

```
Source (.c2)
  │
  ├── 1. Lexer       — Tokenizes C² source (C23 tokens + C²: borrow, own, when, :=, [[, ]])
  ├── 2. Parser      — Recursive-descent to AST (contracts, derivations, ownership)
  ├── 3. Type Check  — Infers types, validates assignments, resolves `result`
  ├── 4. VRP         — Infers integer variable ranges (Value Range Propagation)
  ├── 5. Borrow Check— Validates 5-state ownership lifecycles (use-after-move, etc.)
  ├── 6. Drop Inject — Inserts destructor calls for owned variables at scope boundaries
  ├── 7. Codegen     — Emits standard C23, strips C² constructs
  │                     (when→if, borrow→restrict, contracts→comments)
  ├── 8. Z3 Verify   — Proves contract pre/post conditions via SMT (advisory)
  └── 9. Synthesis   — Derives implementations from examples (c2c derive)
                      ↓
           C source → GCC/Clang → Binary
```

## Features

### Contracts (`[pre][post]`)

Declare preconditions and postconditions before any function.
The Z3 SMT solver proves them at compile time.

```c
[denom != 0][result == num / denom]
int32_t divide(int32_t num, int32_t denom) {
    return num / denom;
}

// Postcondition only (double-open = no pre):
[[result >= 0]
int32_t abs_value(int32_t x) { ... }

// Precondition only (double-close = no post):
[x != 0]]
int32_t invert(int32_t x) { ... }
```

### Borrow Checking & Ownership

Lexical ownership tracking with 5 states (`UNINITIALIZED → OWNED → BORROWED/MOVED → DROPPED`).
The borrow checker validates every variable access.

```c
void read(borrow int32_t* p) { int32_t x = *p; }  // read-only, no mutation
void write(own int32_t* p)   { *p = 42; }           // mutable, owned
void example() {
    own int32_t* data = malloc(100);
    read(data);    // borrow — OK
    free(data);    // early free — transitions to DROPPED
    free(data);    // compile error: double-free
}
```

### Drop Injection

Owned variables automatically get destructor calls at scope boundaries.
Early `free()` transitions OWNED → DROPPED, suppressing the injected drop.

### Value Range Propagation (VRP)

Infers integer ranges from assignments, for-loop headers, and when-guards.
Feeds into the codegen for `__builtin_unreachable()` hints.

### Program Synthesis (`c2c derive`)

Write input-output examples and let the compiler generate the implementation
via cost-guided enumerative search with Pareto frontier optimization.

```c
float sqrt_approx(float x) := {
    0.0   -> [0.01] 0.0;
    1.0   -> [0.01] 1.0;
    4.0   -> [0.5]  2.0;
    9.0   -> [1.0]  3.0;
} [budget=6, ops={+, -, *, /}];
// c2c derive: finds `x * 0.5 + 0.5` as the best approximation
```

### Verification (`c2c verify`)

Derivation examples serve as compile-time assertions.
On each `c2c build`, the verify pass compiles a test harness, runs it,
and reports PASS/FAIL per example — a built-in regression test.

### Optimizing Codegen

- `borrow int32_t*` → `const int32_t *restrict` (non-aliasing, read-only)
- `own int32_t*` → `int32_t *restrict` (non-aliasing, mutable)
- VRP-proven branches → `__builtin_unreachable()` hints

## Quick Start

```bash
# Prerequisites
sudo apt-get install -y libz3-dev    # Z3 SMT solver for contract verification

# Build the c2c compiler
make

# Build a C² program (transpile + GCC compile)
./build/c2c build examples/swap_bytes.c2 -o swap_bytes

# Check contracts and types only
./build/c2c check examples/test_minimal.c2

# Transpile to C only
./build/c2c build examples/swap_bytes.c2 --emit-c

# Synthesize implementations from examples
./build/c2c derive examples/sqrt_approx.c2

# Run tests
make test
```

## Commands

| Command | Description |
|---------|-------------|
| `c2c build <file>` | Transpile → compile → binary (driver mode) |
| `c2c build <file> --emit-c` | Transpile to C only |
| `c2c check <file>` | Lex → parse → typecheck → verify contracts |
| `c2c verify <file>` | Parse → codegen → compile → run derivation tests |
| `c2c derive <file>` | Enumerative synthesis from examples → source mutation |

## Example

```c
#include "c2.h"

[denom != 0][result == num / denom]
int32_t divide(int32_t num, int32_t denom) {
    return num / denom;
}

// Postcondition-only — compiler proves abs_value never returns negative
[[result >= 0]
int32_t abs_value(int32_t x) {
    when x < 0 -> return -x;
    return x;
}

// Derivation block with tolerance annotations for float output
int32_t add(int32_t a, int32_t b) := {
    1, 2 -> 3;
    10, 20 -> 30;
} { return a + b; }

int32_t main(void) {
    printf("divide(10, 3) = %d\n", divide(10, 3));
    printf("abs_value(-5) = %d\n", abs_value(-5));
    return 0;
}
```

## Test Suite

```
make test    # 180+ tests across 9 test suites
```

| Suite | Tests | What |
|-------|-------|------|
| `test_lexer` | 58 | Tokenization, keywords, operators |
| `test_parser` | 33 | AST construction, error recovery, contracts |
| `test_codegen` | 19 | C emission, when→if, derivation comments |
| `test_typecheck` | 33 | Type inference, assignments, contracts |
| `test_verifier` | 13 | Z3 proof queries, pre/post validation |
| `test_vrp` | 6 | Range inference, for-loop bounds, when guards |
| `test_borrow` | 3 | State transitions, read/write/drop |
| `test_drop` | 1 | Drop call injection |
| `test_derive` | 4 | Expression enumeration, Pareto frontier |
| `test_full_pipeline` | 10 | End-to-end: C² → transpile → GCC → run |

## Project Status

| Phase | Module | Status | Tests |
|-------|--------|--------|-------|
| A | Lexer | Complete | 58 |
| A | Parser | Complete | 33 |
| A | AST | Complete | — |
| A | Codegen | Complete | 19 |
| A | Verify (derivations) | Complete | — |
| B | Type checker | Complete | 33 |
| C | Z3 contract verifier | Complete | 13 |
| D | VRP (Value Range Propagation) | Complete | 6 |
| E | Borrow checker | Complete | 3 |
| E | Drop injection | Complete | 1 |
| F | Program synthesis engine | Complete | 4 |
| G | Optimizing codegen | Complete | — |
| H | Self-hosting | Planned | — |

## Language Syntax Quick Reference

```c
// Contracts (mandatory for all functions)
[precondition][postcondition]
[[postcondition]                 // double-open = no pre
[precondition]]                  // double-close = no post

// Variables and ownership
borrow int32_t* p;              // read-only reference
own int32_t* q;                 // owned mutable pointer

// Control flow (no if/else, no goto)
when condition -> statement;    // single-line guard
when condition { ... }          // block guard

// Derivation
:= { input -> output; }         // assertion or synthesis spec

// Budget for synthesis
} [budget(N, ops={+, -, *})];   // operation budget
```

## Documentation

- `docs/spec.md` — Full language specification
- `docs/plans/` — Implementation plans for each phase
- `docs/architecture/` — Architecture decision records
- `AGENTS.md` — Coding standards and project conventions

## License

TBD — to be determined before first public release.
