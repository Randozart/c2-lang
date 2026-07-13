# Phase F — Program Synthesis Engine: Cost-Guided Expression Derivation

**Date:** 2026-07-13  
**Scope:** Full design for `c2c derive` — an enumerative expression synthesizer that
          balances operation count against derivation accuracy using hard/soft constraints,
          Pareto frontier exploration, and bounded search.  
**Status:** Design / Pre-Implementation

---

## 1. Motivation

The Quake fast inverse square root (`0x5F3759DF`) is the canonical example of
a human-discovered numeric hack. Can a compiler rediscover it — or something
like it — from examples alone?

**This phase answers that question** by building a synthesis engine that:

- Accepts **hard examples** (exact match required) and **soft examples** (match
  within tolerance, trading accuracy for fewer operations)
- Searches from smallest to largest expression trees (bounded by a user budget)
- Returns the **Pareto frontier** of (operation count, total error) pairs
- Synthesizes the expression at the user's preferred trade-off point
- Injects the result into the source file as compilable C

---

## 2. Syntax

### Examples with optional tolerance

```
float inv_sqrt(float x) := {
    // Hard: exact at identity
    1.0 -> 1.0;
    4.0 -> 0.5;

    // Soft: within 5% tolerance
    0.5  -> [0.05] 0.7071;
    2.0  -> [0.05] 1.4142;
    0.25 -> [0.1]  2.0;
    9.0  -> [0.1]  0.3333;

    // Hard: tolerance = 0 (equivalent to no bracket)
    100.0 -> [0] 0.1;
};
```

Tolerance syntax: `input -> [tolerance_value] output;`  
When no bracket is present, tolerance defaults to `0` (hard constraint).

### Operation budget and allowed ops

Syntax after the derivation block's closing `}`:

```
} [budget(N, ops={+, -, *, &, |, <<, >>})];
```

Where:
- `budget` = max operations (inclusive). The engine enumerates 1..N.
- `ops` = comma-separated set of allowed operator tokens.

If `ops` is omitted, all arithmetic and bitwise operators are allowed.

If the entire `[budget(...)]` is omitted, defaults to `budget=7, ops={+,-,*,&,|,<<,>>,~}`.

### Function-level budget override

```
[budget(N)]
float inv_sqrt(float x) := { ... };
```

Placed before the function signature (like contract brackets), this sets the
budget for the entire function, overriding any per-block setting.

---

## 3. Cost Model

### Hard constraints (tolerance = 0)

Must be satisfied exactly. If not, the candidate is rejected immediately.

### Soft constraints (tolerance > 0)

Each soft example contributes a penalty:

```
error(e, c)   = |eval(c, inputs(e)) - output(e)|
raw_penalty(e, c) = max(0, error(e, c) - tolerance(e))
penalty(e, c) = raw_penalty(e, c) / max(tolerance(e), 1e-10)
```

Penalty is **normalized by tolerance** so that a 1% overshoot on a 1% tolerance
example costs the same as a 1% overshoot on a 5% tolerance example.

### Total cost

```
cost(candidate) = op_count(candidate) + sum(penalty(e, candidate) for soft examples e)
```

Both terms are unitless: operation count and penalty are summed directly.

**Consequence:** Adding one extra operation is "worth it" if it reduces the sum
of normalized penalties by at least 1. This gives a natural preference for
simpler expressions unless accuracy significantly improves.

---

## 4. Search Strategy

### 4.1 Enumerative Bounded Search

Since Z3 SMT-based synthesis is not yet integrated (Phase C), the initial
engine uses **depth-bounded enumeration**:

1. Define a set of **operation templates** from the user's `ops` set.
2. Enumerate expression trees in order of **operation count** (1, 2, 3, ... up to `budget`).
3. For each tree, fill leaves with:
   - The function's parameter variables (e.g., `x`)
   - Integer constant literals: common small constants first (`0`, `1`, `2`, `-1`, `3`, `4`, `5`, `10`, `16`, `32`, `255`, `256`, `0x5F3759DF`, ...)
   - Float constant literals: (`0.0`, `0.5`, `1.0`, `2.0`, `0.25`, `0.3333`, ...)
4. Evaluate each candidate on all hard examples. Reject if any hard example fails.
5. Evaluate on soft examples. Compute `cost(candidate)`.
6. Track the **Pareto frontier**: candidates where no other candidate is both
   cheaper AND has lower total penalty.

### 4.2 Operation Templates

For `ops={+, -, *, &, |, <<, >>}`:

```
BinaryOp(left, right, OP)      — left OP right
UnaryOp(operand, OP)           — OP operand  (for ~ and -)
Variable(name)                 — read a parameter
Constant(value)                — literal
Ternary(cond, then, else)      — cond ? then : else  (if in ops set)
```

To avoid semantically meaningless expressions, a few pruning rules:
- `x >> y` where `y` is negative → reject (undefined behavior in C)
- `x / y` where `y` is a constant 0 → reject
- `x % y` where `y` is a constant 0 → reject

### 4.3 Constant Discovery

The engine maintains a **constant bank** — a prioritized list of constants
to try as leaf values:

| Priority | Constant | Rationale |
|----------|----------|-----------|
| 0 | `0`, `1` | Identity/annihilator for many ops |
| 1 | `-1` | Bitwise NOT via `x ^ -1` |
| 2 | `2`, `0.5` | Halving/doubling |
| 3 | `255`, `256`, `65535` | Byte/word masks |
| 4 | `0xFFFFFFFF`, `0x80000000` | Sign bits, full masks |
| 5 | `0x5F3759DF`, `0x5F375A86` | Known magic constants |
| 6 | Small primes, powers of 2 | General utility |

Constants are tried in priority order. The search stops trying constants
for a given expression shape once it finds a candidate that satisfies all
hard examples (but continues to higher operation counts).

### 4.4 The `[budget(N)]` semantic

`N` is the **maximum operation count** for synthesis. The engine:
- Starts with 1-op expressions: `x`, `0`, `1`, `~x`, `-x`
- Then 2-op: `x + 1`, `x & 255`, etc.
- Continues up to N
- For each op count, tries all tree shapes and constant combinations

If the engine reaches N without finding any candidate that satisfies the
hard examples, it reports: `No expression found within budget. Try increasing
budget or adding more op types.`

---

## 5. Output: Pareto Frontier

After the search completes, the engine returns the Pareto frontier — all
candidates where no other candidate dominates them:

```
c2c derive approx_sqrt.c2

Searching for expression (budget=7, ops=all)...

Pareto frontier:
  ops=1  error=0.42   x
  ops=2  error=0.18   x * 0.5
  ops=3  error=0.09   x * 0.5 + 0.5
  ops=5  error=0.003  (x * 0.5 + 0.5) * 0.9 + 0.1

Select expression (default: knee at ops=3): [enter]
Synthesized body for 'approx_sqrt':
    return x * 0.5f + 0.5f;
```

The **knee** is the candidate with the best trade-off — the point where
adding more operations yields diminishing accuracy returns. By default,
the knee is selected, but the user can pick any candidate from the frontier.

### Pareto Knee Detection

```
knee = argmin over candidates of sqrt(op_count^2 + penalty^2)
```

I.e., the point on the frontier closest to the origin in
(operations, penalty) space.

---

## 6. Source Mutation

After the user selects a candidate, the engine surgically inserts the body:

1. Record the exact byte offset after the closing `)` of the parameter list
   (or before `:=`).
2. Generate: `{\n    return EXPRESSION;\n}`
3. Open the source file as a byte array.
4. Insert the body string at the recorded offset.
5. Write the result back to disk.
6. The derivation block stays in place as a permanent assertion.

Result:

```c
float approx_sqrt(float x) {
    return x * 0.5f + 0.5f;
} := {
    1.0 -> 1.0;
    4.0 -> 0.5;
    0.5 -> [0.05] 0.7071;
    2.0 -> [0.05] 1.4142;
};
```

On subsequent `c2c build`, the verify pass checks the synthesized body against
the derivations — a built-in regression test.

---

## 7. Implementation Plan

### Phase F.1: Expression Enumeration (Week 1)

**Files:** `src/derive.c`, `src/derive.h`

- Expression tree data structure
- Tree enumeration: generate all trees up to N ops
- Leaf filling: variables + constant bank
- Evaluation on example inputs (tree-walking interpreter)
- Hard-constraint checking
- Pareto frontier tracking

### Phase F.2: Cost + Budget (Week 1, days 3-5)

- Soft-constraint penalty calculation
- `budget` syntax parsing (post-derivation-block `[budget(N, ops={...})]`)
- Parsing for function-level `[budget(N)]` attribute
- Operation cost table (from spec §6.5)
- Pruning: reject UB expressions

### Phase F.3: User Interaction + Source Mutation (Week 2, days 1-2)

- CLI interaction: print Pareto frontier, prompt for selection
- Byte-offset tracking in the parser for surgical insertion
- Source file mutation (read → insert → write)
- `c2c derive` command dispatch (already stubbed in main.c)

### Phase F.4: Tests + Polish (Week 2, days 3-5)

- Unit tests: expression enumeration, cost calculation, Pareto frontier
- Integration tests: derive a trivial function from examples
- Regression tests: ensure source mutation preserves formatting
- Documentation update

---

## 8. Future Enhancements (Post-Phase F)

### SMT-Guided Search (Phase C integration)

Once Z3 is integrated (Phase C), the synthesis engine can use:
- Component-based synthesis: Z3 selects constants instead of brute-force
- Verification: Z3 proves the synthesized expression satisfies hard constraints
  for ALL inputs, not just the examples
- Unbounded search: Z3 can generalize beyond the example set

### Recursive Derivation

For recursive functions, the engine could:
- Detect that examples follow a pattern (e.g., `f(0)=0, f(1)=1, f(2)=3, f(3)=6`)
- Recognize the pattern as triangular numbers: `f(n) = n*(n+1)/2`
- Synthesize the closed form

### Auto-Budget

If no budget is specified, the engine could:
- Start with `budget=1`
- Double until a candidate satisfies hard examples
- Report the lowest budget that works

---

## 9. Example Walkthrough: Approximating `sqrt(x)`

### Input

```c
float sqrt_approx(float x) := {
    0.0   -> [0.01] 0.0;
    0.25  -> [0.1]  0.5;
    1.0   -> [0.01] 1.0;
    2.0   -> [0.3]  1.414;
    4.0   -> [0.5]  2.0;
    9.0   -> [1.0]  3.0;
    100.0 -> [5.0]  10.0;
} [budget=6, ops={+, -, *, /}];
```

### Search

| Ops | Candidate | Hard OK? | Penalt | Cost |
|-----|-----------|----------|--------|------|
| 1 | `x` | Yes | 7.2 | 8.2 |
| 2 | `x * 0.5` | Yes | 1.7 | 3.7 |
| 3 | `x * 0.5 + 0.5` | Yes | 0.35 | 3.35 ← knee |
| 4 | `(x + 1) * 0.5` | Yes | 0.38 | 4.38 |
| 5 | `(x * 0.5 + 0.5) * 0.9` | Yes | 0.18 | 5.18 |
| 6 | `(x * 0.5 + 0.5) * 0.9 + 0.1` | Yes | 0.02 | 6.02 |

### Result

Knee at ops=3: `x * 0.5 + 0.5`. This is the same crude approximation from the
trophy bug — but now the engine discovered it from examples alone.

---

## 10. Key Risks

| Risk | Mitigation |
|------|------------|
| **Combinatorial explosion** at budget > 7 | Prune redundant shapes (commutative ops: `a+b` = `b+a`). Limit constant bank for higher budgets. |
| **User picks an overfitted expression** that works on examples but not in general | The verify pass catches this on subsequent `c2c build` runs. Z3 integration (Phase C) would prove correctness for ALL inputs. |
| **Float equality is fragile** | Tolerance annotations are required for float outputs. Hard constraints on floats only work with tolerance=0 and exact bit-exact match. |
| **UB expressions crash the evaluator** | The interpreter uses saturating/wrapping semantics to avoid crashes. Division by zero → inf. |
