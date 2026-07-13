# Learn C²

C² (Contract Enforced C) is a **superset of C23** that adds compile-time
contracts, ownership-based memory management, and program synthesis —
without changing the C ABI or adding any runtime overhead.

If you know C, you already know most of C². This guide covers what's different.

## Contents

| Lesson | What You'll Learn |
|--------|-------------------|
| [01 — Contracts](01-contracts.md) | Pre/post conditions, `[pre][post]` syntax, `result` variable |
| [02 — Control Flow](02-control-flow.md) | `when` instead of `if`/`else`/`goto`, sequential guards |
| [03 — Ownership](03-ownership.md) | `borrow`/`own` qualifiers, state machine, drop injection |
| [04 — Derivation & Synthesis](04-derivation.md) | Examples as assertions, `c2c derive`, Pareto optimization |
| [05 — Build System](05-build-system.md) | `build`, `check`, `verify`, `derive` commands, pipeline |
| [06 — Full Example](06-full-example.md) | Complete program tying everything together |

## Quick Cheat Sheet

```c
// C                          // C²
if (x > 0) { ... }            when x > 0 -> statement;
if (x > 0) { ... } else       when x > 0 -> stmt; when x <= 0 -> stmt;
goto cleanup;                 // not available — use when guards
int32_t* p;                   int32_t* p;
const int32_t* p;             borrow int32_t* p;
int32_t* restrict p;          own int32_t* p;
// (no contract)              [precondition][postcondition]
// (no derivation)            := { input -> output; }
```

## Philosophy

C²'s core idea: **move safety verification from runtime to compile time**.

- **Contracts** replace runtime assertions with Z3 SMT proofs
- **Borrow checking** replaces garbage collection with lexical ownership
- **When guards** replace `if`/`else` chains with flat, provable control flow
- **Derivation** replaces unit tests with compile-time-checked specifications

The emitted C code is ordinary C23 — no runtime, no GC, no hidden costs.
