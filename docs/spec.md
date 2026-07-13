# C² (Contract Enforced C, Squared with Brackets) — Compiler Specification

**Document Version:** 1.0.0 (2026-07-13)  
**Implementation Language:** Pure C23  
**Target Output:** Standard C99/C23 compatible with GCC 13+, Clang 18+, or TinyCC  
**Verification Engine:** Z3 SMT Solver via the native `z3.h` C API  
**License:** TBD  

---

## Table of Contents

1. [Introduction & Core Concept](#1-introduction--core-concept)
2. [Language Syntax](#2-language-syntax)
   - 2.1 [Contracts (`[pre][post]`, `[[post]`, `[pre]]`)](#21-contracts)
   - 2.2 [Guards (`when`)](#22-guards-when)
   - 2.3 [Ownership Modifiers (`borrow`, `own`)](#23-ownership-modifiers-borrow-own)
   - 2.4 [Derivation Blocks (`:=`)](#24-derivation-blocks-)
   - 2.5 [The `no_derive` Pragma](#25-the-no_derive-pragma)
   - 2.6 [Falsifiability Requirement](#26-falsifiability-requirement)
   - 2.7 [Formal EBNF Grammar](#27-formal-ebnf-grammar)
3. [Compiler Architecture](#3-compiler-architecture)
   - 3.1 [The 7-Phase Pipeline](#31-the-7-phase-pipeline)
   - 3.2 [Phase 1: Lexer & Parser](#32-phase-1-lexer--parser)
   - 3.3 [Phase 2: Type & Arity Validator](#33-phase-2-type--arity-validator)
   - 3.4 [Phase 3: Value Range Propagation](#34-phase-3-value-range-propagation)
   - 3.5 [Phase 4: Z3 SMT Contract Verifier](#35-phase-4-z3-smt-contract-verifier)
   - 3.6 [Phase 5: Lexical Borrow Checker](#36-phase-5-lexical-borrow-checker)
   - 3.7 [Phase 6: Drop Injection Pass](#37-phase-6-drop-injection-pass)
   - 3.8 [Phase 7: Optimizing C Code Generator](#38-phase-7-optimizing-c-code-generator)
4. [Static Verification Engine](#4-static-verification-engine)
   - 4.1 [Array Bounds Verification](#41-array-bounds-verification)
   - 4.2 [Contract Proof](#42-contract-proof)
   - 4.3 [Pointer Non-Overlap Proofs](#43-pointer-non-overlap-proofs)
   - 4.4 [Falsifiability Verification](#44-falsifiability-verification)
5. [Memory Management](#5-memory-management)
   - 5.1 [Variable State Machine](#51-variable-state-machine)
   - 5.2 [State Transitions](#52-state-transitions)
   - 5.3 [Automatic Drop Injection](#53-automatic-drop-injection)
   - 5.4 [Borrow Checking Rules](#54-borrow-checking-rules)
   - 5.5 [Move Semantics](#55-move-semantics)
6. [Derivation & Synthesis Engine](#6-derivation--synthesis-engine)
   - 6.1 [Compile-Time Assertion Mode](#61-compile-time-assertion-mode)
   - 6.2 [Program Synthesis Mode](#62-program-synthesis-mode)
   - 6.3 [SMT-Based Constraint Solving](#63-smt-based-constraint-solving)
   - 6.4 [Bounded Enumerative Search Fallback](#64-bounded-enumerative-search-fallback)
   - 6.5 [Occam's Razor Cost Model](#65-occams-razor-cost-model)
   - 6.6 [Surgical Source-Code Insertion](#66-surgical-source-code-insertion)
7. [Optimizing Code Generation](#7-optimizing-code-generation)
   - 7.1 [Bounds Contract Pragma Emission](#71-bounds-contract-pragma-emission)
   - 7.2 [Non-Overlap Optimization (`restrict`)](#72-non-overlap-optimization-restrict)
   - 7.3 [Non-Null Promotion (`__attribute__((nonnull))`)](#73-non-null-promotion-__attribute__nonnull)
   - 7.4 [Contract-to-Pragma Mapping Table](#74-contract-to-pragma-mapping-table)
8. [CLI Interface](#8-cli-interface)
   - 8.1 [Commands](#81-commands)
   - 8.2 [Flags](#82-flags)
   - 8.3 [Exit Codes](#83-exit-codes)
9. [Standard Library (`c2.h`)](#9-standard-library-c2h)
10. [Project Structure](#10-project-structure)
11. [Implementation Plan](#11-implementation-plan)

---

## 1. Introduction & Core Concept

C² (pronounced "C-squared" or "Contract Enforced C") is a **C-to-C semantic transpiler** that extends standard C23 with compile-time verified contracts, automatic memory management, and optional program synthesis. It follows the proven bootstrapping strategy used by C++'s `cfront` and TypeScript: a custom frontend parses extended syntax, verifies safety properties, and emits standard C code that any conforming C compiler can compile.

### 1.1 Design Tenets

1. **Zero runtime overhead.** All safety checks happen at compile time. The emitted C code contains no runtime guards unless the source explicitly writes `when` statements.
2. **Drop-in compatibility.** C² programs can include any existing C library. The `c2.h` header ensures the C² syntax degrades gracefully when compiled by standard C compilers.
3. **Self-hosting.** The C² compiler is written in pure C. Once minimally functional, the compiler compiles its own source code, mathematically proving its own memory safety.
4. **Gradual adoption.** A `.c2` file can use as few or as many C² features as desired. The compiler enforces only what the developer opts into.

### 1.2 The Transpilation Pipeline

```
[c2 source]  ─→  Parser  ─→  VRP Pass  ─→  Z3 Verifier  ─→  Borrow Checker
                                                              │
                                                              v
                                              Drop Injection  ─→  C Codegen
                                                                    │
                                                                    v
                                                       [standard .c file]
                                                                    │
                                                                    v
                                                      GCC / Clang / TCC
                                                                    │
                                                                    v
                                                           [native binary]
```

When compiling in **driver mode** (default), C² automatically pipes the generated C code to the system C compiler. In **transpile-only mode** (`--emit-c`), it writes the `.c` file and exits.

---

## 2. Language Syntax

C² is a superset of C23. Every valid C23 program is a valid C² program. The extensions described below are parsed natively by the C² frontend and desugared to standard C during code generation.

### 2.1 Contracts

A contract is a pair of expressions, written in square brackets, that declare a function's precondition and postcondition. Contracts appear on the line immediately preceding the function signature.

#### Three Valid Forms

| Syntax | Precondition | Postcondition | Notes |
|--------|-------------|---------------|-------|
| `[pre][post]` | `pre` | `post` | Full form |
| `[[post]` | `true` | `post` | Double-open bracket = pre is omitted (defaults to `true`) |
| `[pre]]` | `pre` | `true` | Double-close bracket = post is omitted (defaults to `true`) |

#### Visual Examples

```c
// Full form: precondition + postcondition
[denom != 0][result == num / denom]
int32_t divide(int32_t num, int32_t denom) {
    return num / denom;
}

// Postcondition only (precondition defaults to true)
[[result >= 0]
int32_t abs_value(int32_t x) {
    return x < 0 ? -x : x;
}

// Precondition only (postcondition defaults to true)
[x >= 0]]
void process_index(int32_t x) {
    // ...
}
```

#### Invalid Forms (Compiler Rejects)

```c
[]        // Both omitted — must declare at least one
[pre]     // Bare single bracket — use [pre] for pre-only or [[] for post-only
]
][
```

The special identifier `result` in a postcondition position refers to the function's return value. If the function returns `void`, `result` is unavailable in the postcondition (compile-time error).

### 2.2 Guards (`when`)

A `when` statement is a single-branch runtime guard that executes its effect *only* if the condition is true. It accepts no `else` block.

> **Design note:** C² deliberately excludes C's `if`/`else` and `goto` statements. All conditional branching uses `when`, which is both safer (no dangling-else ambiguity) and simpler (single branch, no fall-through). The `when` construct is desugared to `if` in emitted C code. If you type `if` or `goto` in C² source, the compiler produces an error with recovery and suggests the canonical `when` form.

#### Syntax

```
when condition -> statement;                // Compact single-line form
when condition { statements... };           // Multi-line block form
```

#### Examples

```c
when denom == 0 -> return 0;

when denom == 0 {
    log_error("Division by zero.");
    return 0;
};
```

`when` is desugared to a standard `if` in the emitted C code (without dangling-else ambiguity, since `when` has no `else` clause).

### 2.3 Ownership Modifiers (`borrow`, `own`)

Pointer parameters must declare their ownership lifecycle using one of two keywords:

- **`borrow`** — Read-only, non-owning reference. Caller retains ownership. The compiler statically prevents mutation or move during the borrow's lifetime.
- **`own`** — Ownership transfer. Caller loses ownership. The function is responsible for dropping the resource.

```c
void print_vector(borrow Vector* vec);      // Read-only borrow
void destroy_vector(own Vector* vec);       // Ownership transfer
```

A parameter with neither `borrow` nor `own` is treated as a standard C pointer — no borrow-checking guarantees are enforced. The compiler emits a warning for any raw pointer parameter in a function that declares contracts.

`borrow` and `own` are desugared to the underlying pointer type in the emitted C code, with `borrow` potentially gaining a `const` qualifier.

### 2.4 Derivation Blocks (`:=`)

A derivation block provides a set of input-output examples that serve as either compile-time assertions or a specification for program synthesis.

#### State A: Synthesized (Body Omitted)

When the function body is omitted, the derivation block follows the signature directly. The compiler (when invoked with `c2c derive`) synthesizes the body.

```c
int32_t swap_bytes(uint16_t val) := {
    0x1234 -> 0x3412;
    0x00FF -> 0xFF00;
};
```

After synthesis succeeds, the compiler **mutates the source file** to insert the body between the signature and the `:=`, leaving the derivation block in place as a permanent compile-time assertion:

```c
int32_t swap_bytes(uint16_t val) {
    return (val << 8) | (val >> 8);
} := {
    0x1234 -> 0x3412;
    0x00FF -> 0xFF00;
};
```

#### State B: Asserted (Body Present)

When the body exists, the derivation block (if present) must follow the closing `}` of the body. The compiler evaluates the body on each input example during every compilation and asserts the outputs match.

```c
int32_t swap_bytes(uint16_t val) {
    return (val << 8) | (val >> 8);
} := {
    0x1234 -> 0x3412;
    0x00FF -> 0xFF00;
};
```

#### Derivation Syntax Rules

- `:=` always follows the function body (if present) or the function signature (if body is absent).
- Each example maps comma-separated inputs to a single output: `input1, input2, ... -> output;`
- The number of inputs must match the function's parameter count.
- Inputs and outputs are compile-time constant expressions.

### 2.5 The `no_derive` Pragma

The `no_derive` keyword, placed before a function signature, prevents the synthesis engine from attempting to generate the body during a `c2c derive` pass. It is used for functions whose specification is not yet ready for automated synthesis.

```c
no_derive
int32_t complex_encryption(uint32_t key) := {
    0x00000000 -> 0xFA421299;
};
```

When `no_derive` is present, the function remains in draft state indefinitely. Removing the keyword signals to the compiler that the specification is ready for synthesis.

### 2.6 Falsifiability Requirement

Every contract expression — whether it appears as a precondition or postcondition — MUST be both **satisfiable** and **falsifiable**. This prevents tautological contracts that provide no safety value.

#### Verification Procedure

For each contract expression *e*:

1. **Satisfiability check:** `Z3_solver_check(ctx, e)` must return SAT. If UNSAT, the expression is a contradiction (e.g., `[1 == 0]`) → **reject**.
2. **Falsifiability check:** `Z3_solver_check(ctx, ¬e)` must return SAT. If UNSAT, the expression is a tautology (e.g., `[1 == 1]`, `[x == x]`) → **reject**.

Both checks must pass for compilation to proceed.

### 2.7 Formal EBNF Grammar

```ebnf
(* Top-level *)
TranslationUnit    ::= { Declaration | FunctionDefinition } ;
FunctionDefinition ::= [NoDerive] Contract? FunctionSignature
                       ( Body [DerivationBlock] | DerivationBlock ) ";" ;

(* Contracts *)
Contract           ::= PreClause PostClause? ;
PreClause          ::= "[" [Expression] "]" ;
PostClause         ::= "[" [Expression] "]" ;
                    (* Constraint: not (PreClause.Expr omitted AND PostClause absent) *)

(* Guards — exclusive conditional construct; C's if/else and goto are NOT part of C² *)
GuardStmt          ::= "when" Expression "->" Statement ";"
                     | "when" Expression "{" { Statement } "}" ";" ;

(* Ownership *)
ParamDecl          ::= [ "borrow" | "own" ] Type Name ;

(* Derivation *)
DerivationBlock    ::= ":=" "{" ExampleList "}" ;
ExampleList        ::= DerivationExample { ";" DerivationExample } [ ";" ] ;
DerivationExample  ::= InputList "->" Expression ;
InputList          ::= Expression { "," Expression } ;

(* Pragma *)
NoDerive           ::= "no_derive" ;

(* Standard C productions *)
FunctionSignature  ::= Type Name "(" ParamList ")" ;
Body               ::= "{" { Statement } "}" ;
Type               ::= (standard C type syntax) ;
Expression          ::= (standard C expression syntax) ;
Statement          ::= (standard C statement syntax, **excluding `if`/`else`/`goto`**;
                      use `when` for conditional branching) ;
```

---

## 3. Compiler Architecture

The C² compiler is organized into seven sequential phases, each of which may halt compilation with a precise error message.

### 3.1 The 7-Phase Pipeline

```
Source ─→ [1 Lexer/Parser] ─→ AST
              │
              v
         [2 Type & Arity Validator] ─→ annotated AST
              │
              v
         [3 Value Range Propagation] ─→ VRP-annotated AST
              │
         ┌────┴────┐
         v         v
    [4 Z3 Verifier]  [5 Borrow Checker]  (parallel, independent)
         │         │
         └────┬────┘
              v
         [6 Drop Injection] ─→ mutated AST
              │
              v
         [7 C Code Generator] ─→ standard C
              │
              v
         [Driver] ─→ GCC/Clang/TCC ─→ native binary
```

Phases 4 and 5 are independent and may run in parallel. If either fails, the compiler halts without proceeding to phase 6.

### 3.2 Phase 1: Lexer & Parser

A hand-written recursive-descent parser in pure C. It produces an AST defined in `ast.h`:

```c
typedef enum {
    NODE_FUNCTION, NODE_CONTRACT, NODE_DERIVATION,
    NODE_WHEN, NODE_BORROW, NODE_OWN,
    NODE_BLOCK, NODE_IF, NODE_WHILE, NODE_FOR,
    NODE_RETURN, NODE_CALL, NODE_INDEX,
    NODE_BINARY_OP, NODE_UNARY_OP, NODE_LITERAL,
    NODE_VARIABLE, NODE_ASSIGN, NODE_DECL,
    // ... additional node kinds
} NodeKind;

typedef struct AstNode {
    NodeKind kind;
    struct AstNode* children;
    size_t child_count;
    Token token;              // Source location info
    // Type-checked metadata (populated in phase 2)
    Type type;
    // VRP metadata (populated in phase 3)
    struct { int64_t lo, hi; } range;
    // Symbol table reference (populated during declaration parsing)
    struct Symbol* symbol;
} AstNode;
```

The parser recognizes all C23 syntax plus the C² extensions (contracts, guards, ownership, derivation). A parallel error-tracking structure records all parse errors before halting.

### 3.3 Phase 2: Type & Arity Validator

Validates:
- Function parameters have correct types and arity.
- Contract expressions are well-typed boolean expressions.
- Derivation example inputs match function parameter count and types.
- `result` in postcondition is only used in non-void functions.
- Ownership modifiers are only applied to pointer types.

### 3.4 Phase 3: Value Range Propagation

The VRP pass analyzes the AST to infer value ranges for variables without requiring explicit contracts:

- Loop induction variables: `for (int i = 0; i < N; i++)` → `i ∈ [0, N-1]`
- Branch constraints: `if (x > 0 && x < 100)` → `x ∈ [1, 99]`
- Assignment propagation: `int y = x + 1` where `x ∈ [0, 5]` → `y ∈ [1, 6]`

The VRP annotates each `AstNode` with a `{lo, hi}` range. These ranges are fed to the Z3 verifier in phase 4 to reduce the number of required explicit contracts.

### 3.5 Phase 4: Z3 SMT Contract Verifier

This phase uses the Z3 C API (`z3.h`) to prove contract correctness:

1. Translate function preconditions into Z3 bit-vector formulas.
2. Assert the precondition in the solver context.
3. Step through each statement in the function body, translating C operations to Z3 formulas.
4. At each array access or contract boundary, ask Z3 to prove the safety condition.
5. If any proof fails, emit a precise compile-time error with source location.

### 3.6 Phase 5: Lexical Borrow Checker

The borrow checker implements the variable state machine (described in [Section 5](#5-memory-management)). It walks the AST and enforces ownership rules without requiring any runtime metadata.

### 3.7 Phase 6: Drop Injection Pass

After verification passes, this phase mutates the AST by injecting destructor calls at scope boundaries:

- Scan each scope's symbol table for variables in `STATE_OWNED` whose type declares a `drop` function.
- Insert a call to the drop function immediately before the scope's closing `}`.
- Variables in `STATE_MOVED` or `STATE_DROPPED` are skipped (prevents double-free).

### 3.8 Phase 7: Optimizing C Code Generator

The code generator walks the mutated AST and produces standard C code. It selectively emits compiler-specific optimization hints based on what the Z3 verifier proved:

- `restrict` for proven non-overlapping pointers
- `__builtin_unreachable()` for proven bounds contracts
- `__attribute__((nonnull))` for proven non-null pointers

Details in [Section 7](#7-optimizing-code-generation).

---

## 4. Static Verification Engine

### 4.1 Array Bounds Verification

When the parser encounters an array access `array[i]`, the verifier:

1. Looks up the array's declared size *N* from the symbol table.
2. Applies VRP-inferred range for `i` (or the current contract context).
3. Generates the Z3 query: `∀ paths. 0 ≤ i < N`
4. If Z3 cannot prove universal safety, compilation is rejected:

```
c2 error: Cannot prove index 'i' is within bounds [0, 99] for 'my_array'
  └─ src/main.c2:42:17
  help: Add a guard or precondition: [i >= 0 && i < 100]
```

### 4.2 Contract Proof

For each function with contracts, the verifier:

1. Asserts the precondition in Z3 context.
2. Symbolically executes the function body.
3. Before the return, asserts that the postcondition must hold.
4. If Z3 finds a counterexample, halts with a precise error.

```c
[denom != 0][result == num / denom]
int32_t divide(int32_t num, int32_t denom) {
    return num / denom;
}
```

### 4.3 Pointer Non-Overlap Proofs

When two `borrow` pointers appear in the same function signature, the verifier checks whether Z3 can prove they do not alias. If proven, the code generator emits `restrict` on both pointers in the emitted C code.

### 4.4 Falsifiability Verification

Per [Section 2.6](#26-falsifiability-requirement), every contract expression is checked for both satisfiability and falsifiability:

```
c2 error: Contract is a tautology and provides no safety value.
  └─ src/main.c2:1:1: [0 == 0]
  help: Write a condition that could potentially be false.
```

---

## 5. Memory Management

C² manages memory at compile time using a lexical state machine. There is no garbage collector, no reference counting, and no runtime overhead.

### 5.1 Variable State Machine

Every local variable is tracked in the compiler's scope stack with one of five states:

```
                    ┌──────────────────┐
                    │  UNINITIALIZED   │
                    └────────┬─────────┘
                             │ allocation / assignment
                             v
                    ┌──────────────────┐
              ┌─────│     OWNED       │──────┐
              │     └────────┬─────────┘     │
              │              │               │
     pass to own     pass &var to        move to
     parameter       borrow param       another var
              │              │               │
              v              v               v
     ┌────────────┐  ┌──────────────┐  ┌──────────┐
     │   MOVED    │  │   BORROWED   │  │  MOVED   │
     └────────────┘  └──────┬───────┘  └──────────┘
                            │ borrow ends
                            v
                       (back to OWNED)
```

```c
typedef enum {
    STATE_UNINITIALIZED,
    STATE_OWNED,
    STATE_BORROWED,
    STATE_MOVED,
    STATE_DROPPED,
} VariableState;
```

### 5.2 State Transitions

| Action | Current State → New State | Condition |
|--------|--------------------------|-----------|
| Variable declared | `UNINITIALIZED → OWNED` | — |
| Assign initial value | `UNINITIALIZED → OWNED` | — |
| Pass to `own` parameter | `OWNED → MOVED` | `borrow_count == 0` |
| Pass `&var` to `borrow` param | `OWNED → BORROWED` | Increment `borrow_count` |
| Borrow scope ends | `BORROWED → OWNED` | `borrow_count` decrements to 0 |
| Mutate while borrowed | **Compiler error** | — |
| Move while borrowed | **Compiler error** | — |
| Scope ends (drop injected) | `OWNED → DROPPED` | Type has `drop` attribute |
| Scope ends (var already moved) | No action | Prevents double-free |
| Use after move | **Compiler error** | — |
| User calls `free(ptr)` | `OWNED → DROPPED` | `borrow_count == 0` |
| User calls `free(ptr)` on moved var | **Compiler error** | Already moved |
| User calls `free(ptr)` again | **Compiler error** | Already `DROPPED` |
| Use after `free()` | **Compiler error** | Already `DROPPED` |

### 5.3 Automatic Drop Injection

When the compiler reaches a closing `}`, it:

1. Iterates all symbols in the current scope.
2. Selects those with `state == STATE_OWNED` whose type implements `[[c2::drop(cleanup_fn)]]`.
3. Injects `cleanup_fn(&var);` before the closing `}` in the AST.

Variables already in `STATE_DROPPED` (because the user called `free()` early) are skipped — no double-free.

#### 5.3.1 Early `free()` Suppression

A manually written `free(ptr)` call causes the borrow checker to transition the owning variable to `STATE_DROPPED`. The drop injection pass then skips it:

```c
// C² source
void example(void) {
    int32_t* buf = malloc(100);
    // ... use buf ...
    free(buf);          // ← early free: buf → DROPPED
    // ... more work without buf ...
};                      // ← no drop injected for buf (already freed)

// Emitted C
void example(void) {
    int32_t* buf = malloc(100);
    // ... use buf ...
    free(buf);
    // ... more work without buf ...
    // ← no free(buf) here — compiler recognized the early free
}
```

The parser recognizes `free(expr)` calls and emits a `NODE_FREE` AST node. The borrow checker handles this node as a state transition (see §5.2).

```c
// C² source
void example(void) {
    Vector vec = create_vector(10);
    // ... use vec ...
};

// Emitted C (drop injected)
void example(void) {
    Vector vec = create_vector(10);
    // ... use vec ...
    free_vector(&vec);  // surgically injected
}
```

### 5.4 Borrow Checking Rules

- Multiple concurrent borrows are allowed (immutable reads only).
- A borrow's lifetime is lexical — it lasts until the end of the expression or scope where the borrow was taken.
- While a borrow is active (`STATE_BORROWED`), the compiler rejects any write, move, or drop of the borrowed variable.
- The borrow checker does **not** prevent data races across threads. Thread safety is a future extension.

### 5.6 Struct Field Interaction

When calling `free(vec->data)` on a struct member, the borrow checker does NOT transition the parent `vec` to `DROPPED`. Instead, the compiler relies on the struct's drop function to be null-safe:

```c
[[c2::drop(free_vector)]]
typedef struct {
    int32_t* data;
    int32_t size;
} Vector;

void free_vector(Vector* vec) {
    if (vec->data) {          // ← null check is required
        free(vec->data);
        vec->data = NULL;
    }
}

void example(void) {
    Vector vec = { malloc(10 * sizeof(int32_t)), 10 };
    free(vec->data);
    vec->data = NULL;          // ← required to prevent double-free
    // vec is dropped here — free_vector(&vec) called, null check passes
}
```

If the user frees a struct member early without setting it to NULL, the drop function will double-free. This is a documented responsibility of the programmer; field-level tracking is a planned future enhancement (see `docs/architecture/2026-07-13-1415-early-free-drop-suppression.md`).

### 5.5 Move Semantics

When an owning variable is passed to an `own` parameter, the variable transitions to `STATE_MOVED`. Subsequent use triggers:

```
c2 error: Use of moved variable 'vec'
  └─ src/main.c2:12:5
  help: Ownership was transferred at src/main.c2:10.
```

---

## 6. Derivation & Synthesis Engine

The derivation block serves dual duty: compile-time assertion (when a body exists) and program synthesis specification (when the body is absent).

### 6.1 Compile-Time Assertion Mode

When a function has both a body and a derivation block, the `c2c build` and
`c2c verify` commands check each example:

1. The compiler evaluates the function body on each input example.
2. It compares the result to the expected output. If a tolerance bracket
   `[tol]` is present, the comparison uses `fabs(result - expected) > tol`.
3. If any assertion fails, compilation halts.

### 6.2 Hard and Soft Examples

Examples fall into two categories:

| Category | Tolerance | Behavior |
|----------|-----------|----------|
| **Hard** | No bracket, or `[0]` | Must match exactly. Candidate is rejected immediately if violated. |
| **Soft** | `[tol]` where `tol > 0` | Error within tolerance passes. Error beyond tolerance contributes a normalized penalty. |

This split lets the synthesis engine know which outputs are negotiable and
which are inviolable.

### 6.3 Operation Budget

A budget annotation on the derivation block limits search space:

```c
} [budget(N, ops={+, -, *, /, &, |, <<, >>})];
```

- `N` = maximum operation count (inclusive). Engine enumerates 1..N.
- `ops` = comma-separated allowed operators. Omit for all ops.

A function-level annotation overrides the block-level:

```c
[budget=7]
float approx_sqrt(float x) := { ... };
```

### 6.4 Cost-Guided Enumerative Search

The engine enumerates expression trees in order of operation count (1..N).
For each operation count, it tries all tree shapes and leaf combinations
(function parameters + prioritized constant bank).

Each candidate is scored:

```
cost(c) = op_count(c) + sum(soft_penalty(e, c) for soft examples e)
```

Where:

```
soft_penalty(e, c) = max(0, |eval(c, inputs(e)) - output(e)| - tolerance(e))
                   / max(tolerance(e), 1e-10)
```

The engine maintains a **Pareto frontier** — candidates where no other
candidate is both cheaper and has lower total penalty.

### 6.5 Pareto Frontier Output

When the search completes, the engine displays the frontier:

```
c2c derive approx_sqrt.c2

Pareto frontier:
  ops=1  error=0.42   x
  ops=3  error=0.09   x * 0.5 + 0.5
  ops=5  error=0.003  (x * 0.5 + 0.5) * 0.9 + 0.1

Select expression (default: knee at ops=3): [enter]
Synthesized body for 'approx_sqrt':
    return x * 0.5f + 0.5f;
```

The **knee** (closest point to origin in (ops, penalty) space) is the default
selection. The user may pick any point on the frontier.

### 6.6 Surgical Source-Code Insertion

On selection, the engine inserts the body into the source file:

1. Record the byte offset after the `)` closing the parameter list (before `:=`).
2. Generate the body string: `{\n    return EXPRESSION;\n}`
3. Open the source file as a byte array.
4. Insert the body string at the recorded offset.
5. Write the result back to disk.

The derivation block remains in place as a permanent assertion for `c2c verify`
on subsequent builds.

### 6.7 Operation Cost Table

| Expression | Cost |
|-----------|------|
| Constant literal | 1 |
| Variable read | 1 |
| Unary `~` | 2 |
| Unary `-` | 2 |
| Bitwise `&`, `|`, `^` | 3 |
| Shift `<<`, `>>` | 3 |
| Arithmetic `+`, `-` | 3 |
| Multiplication `*` | 4 |
| Division `/` | 5 |
| Ternary `?` `:` | 5 |

---

## 7. Optimizing Code Generation

The C² code generator emits optimization hints that are **mathematically guaranteed to be correct** because Z3 proved them.

### 7.1 Bounds Contract Pragma Emission

When the verifier proves an index is always in-bounds (via contract or VRP), the codegen emits:

```c
// If [x < 100 && x > 0] was proven:
if (x <= 0 || x >= 100) __builtin_unreachable();
// Downstream compiler knows x ∈ [1, 99] and eliminates bounds branches.
```

### 7.2 Non-Overlap Optimization (`restrict`)

When Z3 proves two `borrow` pointers do not alias, the codegen emits:

```c
// C²: void transform(borrow Vector* a, borrow Vector* b)
// Emitted C:
void transform(Vector* restrict a, Vector* restrict b)
```

### 7.3 Non-Null Promotion (`__attribute__((nonnull))`)

When a `borrow` pointer is proven non-null:

```c
// C²: void process(borrow Vector* vec)
// Emitted C:
void process(Vector* vec) __attribute__((nonnull(1)));
```

### 7.4 Contract-to-Pragma Mapping Table

| Contract Pattern | Proof | Emitted Annotation |
|------------------|-------|-------------------|
| `[lo <= i < hi]` index guard | Index always in-bounds | `if (i < lo \|\| i >= hi) __builtin_unreachable();` |
| Two `borrow T* a, b` | Pointers do not alias | `T* restrict a, T* restrict b` |
| `borrow T* p` | `p` is never null | `__attribute__((nonnull(1)))` |
| `[pre]` where `pre` is range | Value `x` within range | Same as index guard pattern |
| `[[pre]` on void function | Precondition proven | `__attribute__((nonnull(...)))` where applicable |

---

## 8. CLI Interface

### 8.1 Commands

| Command | Description |
|---------|-------------|
| `c2c build <file> [flags]` | Transpile and compile to binary (driver mode) |
| `c2c check <file>` | Verify contracts and borrows only; no codegen |
| `c2c derive <file>` | Synthesize implementations for functions in draft state |
| `c2c derive --all <path>` | Project-wide synthesis over all `.c2` files |

### 8.2 Flags

| Flag | Description |
|------|-------------|
| `-o <path>` | Output binary path (default: `a.out`) |
| `--emit-c` | Transpile-only mode; write `.c` file, do not invoke C compiler |
| `--emit-c-dir <dir>` | Directory for emitted `.c` files (default: `./_c2_out`) |
| `--cc <compiler>` | C compiler to invoke (default: auto-detect `gcc` or `clang`) |
| `--cc-flags <flags>` | Additional flags to pass to the C compiler |
| `--z3-path <path>` | Path to Z3 shared library (default: `libz3.so`) |
| `--no-verify` | Skip verification (emit C without proofs) |
| `--check-only` | Alias for `check` subcommand behavior |

### 8.3 Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Parse error |
| 2 | Verification error (contract/bounds proof failed) |
| 3 | Borrow checker error |
| 4 | Derivation assertion failed |
| 5 | Synthesis failed (no expression found) |
| 6 | Compiler invocation failed |
| 7 | Internal compiler error |

---

## 9. Standard Library (`c2.h`)

The `c2.h` header ensures C² code can be compiled by standard C compilers without C²'s verification (attributes degrade to no-ops).

```c
// c2.h — Compatibility header for C² (Contract Enforced C)
//
// When compiled by c2, this header is not used (the parser handles
// contracts natively). When compiled by a standard C compiler,
// the C23 attributes degrade to no-ops and [[c2::drop]] is
// emitted as a call to the cleanup function via __attribute__((cleanup)).
//
// 2026-07-13 — Initial version.

#ifndef C2_H
#define C2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ── C23 attribute compatibility ────────────────────────────────────────
// Standard C23 compilers (GCC 13+, Clang 18+) must ignore attributes
// in unrecognized namespaces per the C23 standard. These macros ensure
// older compilers or C99 mode also ignore them safely.

#ifndef __c2__
    // Squelch C23 attributes when not compiling with c2
    #ifndef __has_c_attribute
        #define C2_EMPTY
    #elif !__has_c_attribute(c2::pre)
        #define C2_EMPTY
    #endif
#endif

// ── GCC/Clang `cleanup` attribute for automatic drop ───────────────────
// When a struct uses [[c2::drop(fn)]], the c2 compiler handles
// destruction. For standard compilers without c2, we provide an
// opt-in macro using GCC/Clang's __attribute__((cleanup)):

#ifdef __GNUC__
    #define C2_DEFER(drop_fn) __attribute__((cleanup(drop_fn)))
#else
    #define C2_DEFER(drop_fn)
#endif

#endif // C2_H
```

---

## 10. Project Structure

```
c2-lang/
├── AGENTS.md                        # Agent guidelines (this file)
├── Makefile                         # Build orchestration
├── README.md                        # Project overview
├── docs/
│   ├── spec.md                      # Language specification (this document)
│   ├── plans/                       # Implementation plans
│   └── architecture/                # Living architecture docs
├── include/
│   └── c2.h                         # Standard library / compatibility header
├── src/
│   ├── main.c                       # CLI entry point, command dispatch
│   ├── lexer.c                      # Tokenizer
│   ├── lexer.h
│   ├── parser.c                     # Recursive-descent parser
│   ├── parser.h
│   ├── ast.h                        # AST node definitions
│   ├── symbol.h                     # Symbol table + scope stack
│   ├── state.h                      # Variable state machine (5-state enum)
│   ├── type.c                       # Type system
│   ├── type.h
│   ├── vrp.c                        # Value range propagation
│   ├── vrp.h
│   ├── verifier.c                   # Z3 SMT integration
│   ├── verifier.h
│   ├── borrow.c                     # Lexical borrow checker
│   ├── borrow.h
│   ├── drop.c                       # Drop injection pass
│   ├── drop.h
│   ├── codegen.c                    # C pretty-printer + optimization hints
│   ├── codegen.h
│   ├── derive.c                     # Program synthesis engine
│   ├── derive.h
│   ├── driver.c                     # GCC/Clang/TCC invocation
│   ├── driver.h
│   └── error.h                      # Error reporting infrastructure
├── tests/
│   ├── lexer/                       # Lexer unit tests
│   ├── parser/                      # Parser unit tests
│   ├── verifier/                    # Z3 verifier integration tests
│   ├── borrow/                      # Borrow checker tests
│   ├── codegen/                     # Codegen output comparison tests
│   └── integration/                 # End-to-end transpile + compile tests
└── examples/
    └── swap_bytes.c2                # Example C² program
```

---

## 11. Implementation Plan

The compiler is built in seven phases, each producing a working (if minimal) compiler:

### Phase A: Foundation (Parser + AST + C Codegen)

**Duration:** ~2 weeks  
**Deliverable:** `c2c build` that can parse a subset of C² (functions, variables, contracts, guards) and emit standard C code — no verification yet.

**Files:**
- `src/main.c`, `src/lexer.c`, `src/parser.c`, `src/ast.h`, `src/codegen.c`
- `include/c2.h`

### Phase B: Type & Contract Validation

**Duration:** ~1 week  
**Deliverable:** Type checking, contract arity validation, derivation example validation, falsifiability checks.

**Files:**
- `src/type.c`, `src/type.h`

### Phase C: Z3 Contract Verification

**Duration:** ~2 weeks  
**Deliverable:** Full Z3 integration for array bounds and pre/post condition verification. Compilation halts with precise errors on contract violation.

**Files:**
- `src/verifier.c`, `src/verifier.h`

### Phase D: Value Range Propagation

**Duration:** ~1 week  
**Deliverable:** Loop induction variable analysis and branch-constraint propagation to reduce explicit contract burden.

**Files:**
- `src/vrp.c`, `src/vrp.h`

### Phase E: Borrow Checker + Drop Injection

**Duration:** ~2 weeks  
**Deliverable:** Variable state machine, lexical borrow checking, automatic destructor injection. C² now provides memory safety.

**Files:**
- `src/borrow.c`, `src/borrow.h`, `src/drop.c`, `src/drop.h`, `src/state.h`, `src/symbol.h`

### Phase F: Program Synthesis

**Duration:** ~2 weeks  
**Deliverable:** `c2c derive` command, Z3-based component synthesis, bounded enumerative fallback, surgical source-code insertion.

**Files:**
- `src/derive.c`, `src/derive.h`

### Phase G: Optimizing Codegen + CLI Polish

**Duration:** ~1 week  
**Deliverable:** `restrict`, `__builtin_unreachable()`, `__attribute__((nonnull))` emission. Full CLI with all flags. Driver mode.

**Files:**
- `src/codegen.c` (enhancements), `src/driver.c`, `src/driver.h`

### Phase H: Self-Hosting Milestone

**Duration:** ~1 week  
**Deliverable:** The C² compiler compiles its own source code. The resulting binary passes all test suites. The project is now self-hosting.

---

## Appendix A: Comparison with Existing Approaches

| Property | Standard C | C² | Rust | Go |
|----------|-----------|-----|------|----|
| Memory safety | Manual | Compile-time (borrow check) | Compile-time (borrow check) | GC runtime |
| Contract verification | None | Z3 SMT | None | None |
| Program synthesis | None | Z3 + enumeration | None | None |
| C ABI compatibility | Native | Native | Via FFI | Via cgo |
| Optimization hints | Manual (unsafe) | Verified automatic | Limited | N/A |
| Self-hosting compiler | Yes | Target | Yes | Yes |
| Learning curve | Steep | Moderate | Steep | Low |

---

## Appendix B: Example Program

```c
// safe_vector.c2 — A complete C² program demonstrating all language features
#include "c2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Owned type with automatic drop ──────────────────────────────────────
typedef struct [[c2::drop(free_vector)]] {
    int32_t* data;
    int32_t size;
} Vector;

void free_vector(Vector* vec) {
    if (vec->data) {
        free(vec->data);
        vec->data = NULL;
    }
    vec->size = 0;
}

// ── Factory with ownership transfer ─────────────────────────────────────
[[result != NULL]
Vector* create_vector(int32_t size) {
    Vector* vec = (Vector*)malloc(sizeof(Vector));
    vec->data = (int32_t*)calloc(size, sizeof(int32_t));
    vec->size = size;
    return vec;
}

// ── Read with borrow and postcondition ──────────────────────────────────
[[result == vec->data[idx]]
int32_t read_vector(borrow Vector* vec, int32_t idx) {
    return vec->data[idx];
}

// ── Write with bounds precondition ──────────────────────────────────────
[idx >= 0 && idx < vec->size]]
void write_vector(borrow Vector* vec, int32_t idx, int32_t val) {
    vec->data[idx] = val;
}

// ── Derivation example (compile-time assertion) ─────────────────────────
int32_t add_and_double(int32_t a, int32_t b) {
    return (a + b) * 2;
} := {
    1, 2 -> 6;
    0, 0 -> 0;
    5, -3 -> 4;
};

// ── Main entry point ────────────────────────────────────────────────────
int32_t main(void) {
    Vector* vec = create_vector(10);

    write_vector(vec, 0, 42);
    write_vector(vec, 1, 99);

    int32_t v0 = read_vector(vec, 0);
    int32_t v1 = read_vector(vec, 1);

    printf("vec[0] = %d, vec[1] = %d\n", v0, v1);
    printf("add_and_double(3, 4) = %d\n", add_and_double(3, 4));

    // vec is automatically freed at scope end via free_vector()
    return 0;
}
```

After transpilation by `c2`, this becomes standard C with bounds checks pruned, `free_vector(&vec)` injected, and optimization hints emitted.

---

*End of specification. This document serves as the single source of truth for the C² language and compiler implementation.*
