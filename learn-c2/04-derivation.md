# 04 — Derivation & Synthesis

## Derivation: Compile-Time Assertions

A **derivation block** (`:= { ... }`) lists input-output examples that the
compiler checks at compile time. Think of it as a unit test that runs
during every build.

```c
int32_t add(int32_t a, int32_t b) {
    return a + b;
} := {
    1, 2 -> 3;
    10, 20 -> 30;
    100, 200 -> 300;
};
```

On every `c2c build` or `c2c verify`, the compiler:
1. Transpiles the function to C (skipping `main()` to avoid linker conflicts)
2. Generates a test harness that calls the function with each example input
3. Compiles and runs the harness
4. Reports PASS/FAIL per example

The derivation block stays in the source as a **permanent regression test**.
If someone refactors `add` to `return a - b` by mistake, the next build
catches it.

## Tolerance Annotations

For floating-point functions where exact equality is fragile, add a
tolerance:

```c
float sqrt_approx(float x) := {
    0.0   -> [0.01] 0.0;       // must match within 1%
    1.0   -> [0.01] 1.0;
    4.0   -> [0.5]  2.0;       // looser tolerance
    9.0   -> [1.0]  3.0;
} { return x * 0.5 + 0.5; }
```

Tolerance syntax: `input -> [tolerance_value] expected_output;`

- `tolerance = 0` (or absent): hard constraint, exact match required
- `tolerance > 0`: soft constraint, match within ±tolerance

## Synthesis: Generate Code from Examples

If a function has a derivation block **but no body**, the compiler can
synthesize the body from the examples:

```c
float sqrt_approx(float x) := {
    0.0   -> [0.01] 0.0;
    1.0   -> [0.01] 1.0;
    4.0   -> [0.5]  2.0;
    9.0   -> [1.0]  3.0;
} [budget=6, ops={+, -, *, /}];
```

Run `c2c derive`:
```
$ c2c derive sqrt_approx.c2

Pareto frontier for 'sqrt_approx' (budget=6):
  ops=1  penalty=7.2000  x
  ops=2  penalty=1.7000  x * 0.5
  ops=3  penalty=0.3500  x * 0.5 + 0.5    ← knee (best trade-off)
  ops=4  penalty=0.3800  (x + 1) * 0.5
  ops=5  penalty=0.1800  (x * 0.5 + 0.5) * 0.9
  ops=6  penalty=0.0200  (x * 0.5 + 0.5) * 0.9 + 0.1
  Knee at ops=3
  Synthesized body for 'sqrt_approx': (x * 0.5 + 0.5)
```

The engine inserts the body into the source file:

```c
float sqrt_approx(float x) {
    return x * 0.5 + 0.5;
} := {
    0.0   -> [0.01] 0.0;
    1.0   -> [0.01] 1.0;
    4.0   -> [0.5]  2.0;
    9.0   -> [1.0]  3.0;
} [budget=6, ops={+, -, *, /}];
```

On all subsequent builds, the verify pass checks the synthesized body
against the derivation examples — a built-in regression test.

## How Synthesis Works

The synthesis engine (`c2c derive`) performs **cost-guided enumerative search**:

1. **Expression enumeration**: generates all expression trees up to `budget` ops
   using the allowed operators from `ops`
2. **Constant bank**: tries common constants (0, 1, -1, 2, 0.5, 255, 256, etc.)
3. **Evaluation**: evaluates each candidate on the example inputs
4. **Hard constraints**: candidates that fail exact-match examples are rejected
5. **Soft constraints**: remaining candidates get a penalty score
6. **Pareto frontier**: tracks (ops, penalty) pairs where no candidate dominates
7. **Knee selection**: chooses the best trade-off (closest to origin)

The budget syntax:
```c
} [budget(N, ops={+, -, *, &, |, <<, >>, ~})];
// Default: budget=7, ops={all arithmetic and bitwise}
```

## Operation Budget

| Budget | What's Searched | Typical Time |
|--------|-----------------|-------------|
| 1–3 | Trivial expressions (x, x+1, etc.) | Instant |
| 4–5 | Simple polynomials | < 1 second |
| 6–7 | Complex expressions | ~5 seconds |
| 8+ | Exponential blowup | Slow |

The expression cap prevents explosion: at most ~50,000 candidates are
generated per function.
