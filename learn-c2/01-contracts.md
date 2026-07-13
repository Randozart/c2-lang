# 01 — Contracts

## What Makes C² Different

Every C² function must declare a **contract** — a precondition and/or
postcondition that the compiler proves at compile time using the Z3 SMT solver.

In C, you write runtime assertions or hope for the best:
```c
// C: runtime check — burns CPU, skipped in release builds
int32_t divide(int32_t num, int32_t denom) {
    assert(denom != 0);
    return num / denom;
}
```

In C², the proof happens at compile time:
```c
// C²: compile-time proof — zero runtime cost
[denom != 0][result == num / denom]
int32_t divide(int32_t num, int32_t denom) {
    return num / denom;
}
```

## Syntax

Contracts go on the line **immediately before** the function signature.
There are three forms:

### Full form: `[precondition][postcondition]`

```c
[x > 0][result == x + 1]
int32_t increment(int32_t x) {
    return x + 1;
}
```

### Postcondition only: `[[postcondition]]`

Double-open bracket `[[` means "no precondition" (default: true).

```c
[[result >= 0]
int32_t abs_value(int32_t x) {
    when x < 0 -> return -x;
    return x;
}
```

### Precondition only: `[precondition]]`

Double-close bracket `]]` means "no postcondition" (default: true).

```c
[x >= 0 && x < 100]]
void set_index(int32_t x) {
    int32_t arr[100];
    arr[x] = 42;
}
```

## The `result` Variable

Inside a **postcondition**, the special identifier `result` refers to
the function's return value:

```c
[denom != 0][result == num / denom]
int32_t divide(int32_t num, int32_t denom) {
    return num / denom;
}
```

The Z3 verifier translates this into a proof query: "is there any input
where `denom != 0` but the return value does NOT equal `num / denom`?"
If the solver finds such an input, the contract FAILS and compilation stops.

Using `result` in a `void` function is a compile error.

## Contract Grammar

```
contract := '[' pre ']' '[' post ']'
         |  '[[' post ']'           // double-open = no pre
         |  '[' pre ']' ']'          // double-close = no post

pre      := <expression returning scalar (bool/int/pointer)>
post     := <expression returning scalar, may reference `result`>
```

## Trivially False Preconditions

The verifier checks that the precondition is **satisfiable** (not trivially
false). This catches bugs like:

```c
[0]        // FAIL: precondition is trivially false
[x != x]   // FAIL: x != x is never true
void f() { }
```

## Behind the Scenes

The Z3 verifier (Phase C) translates contract expressions to bit-vector
formulas and runs two proof queries:

1. **Precondition satisfiability**: `Z3_solver_check(pre)` — must be SAT
2. **Postcondition validity**: `Z3_solver_check(pre AND NOT(post))` — must be UNSAT

If either check fails, the contract is reported as violated (non-blocking —
the compilation continues, but you see the warning).

## Limitations

- Contracts with loops are skipped by the verifier (loop invariant
  inference is a future enhancement)
- Float comparisons use Z3 bit-vector semantics (not IEEE 754 special values)
- Pointer contracts (e.g., `[p != NULL]`) work as long as the pointer
  variable is a function parameter
