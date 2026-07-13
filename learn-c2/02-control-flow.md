# 02 — Control Flow

## Why No `if`/`else`?

C² deliberately excludes `if`, `else`, and `goto`. Why?

- **Provability:** `when` guards create flat control flow that the Z3
  verifier and VRP pass can reason about precisely
- **Safety:** Nested `if`/`else` chains are the #1 source of logic errors.
  Sequential `when` guards are easier to verify and maintain
- **No `goto`:** C² enforces structured programming by fiat

If you write `if` or `goto`, the compiler prints a helpful error:

```
error: `if` is not a C² construct.
  Use `when` instead:
    when condition -> expression;
    when condition { statements }
```

## `when` Guards — Arrow Form

Single-line guard with an arrow `->`:

```c
when x > 0 -> return x;
```

This emits as `if (x > 0) { return x; }` in the generated C code.

## `when` Guards — Block Form

Multi-line guard with braces:

```c
when x > 0 {
    int32_t y = x * 2;
    return y;
}
```

## Sequential `when` Guards (Replaces `if`/`else if`/`else`)

```c
// C: nested if/else chain
if (x > 0) {
    return 1;
} else if (x < 0) {
    return -1;
} else {
    return 0;
}

// C²: flat sequential guards
when x > 0 -> return 1;
when x < 0 -> return -1;
return 0;
```

Each `when` guard is independent. If the condition is true, the statement
executes. If not, execution falls through to the next guard. No nesting.

This pattern is verified by the borrow checker: if the first `when` returns,
the variable state is MOVED. If it doesn't return, the variable is still
OWNED for the next guard.

## Loops

C² supports `while`, `for`, and `do-while` with standard C syntax:

```c
// while
int32_t i = 0;
while (i < 10) {
    do_work(i);
    i = i + 1;
}

// for (including declaration in init)
for (int32_t i = 0; i < 10; i++) {
    do_work(i);
}

// do-while
int32_t x = 0;
do {
    process(x);
    x = x + 1;
} while (x < 10);
```

## `switch`/`case`/`default`

C² supports switch statements with standard C syntax:

```c
switch (x) {
    case 0 -> return "zero";
    case 1 -> return "one";
    default -> return "many";
}
```

## Comparison: C vs C²

| C | C² |
|---|----|
| `if (cond) { ... }` | `when cond -> stmt;` or `when cond { ... }` |
| `if (cond) { ... } else { ... }` | `when cond -> stmt; when !cond -> stmt;` |
| `if (a) { ... } else if (b) { ... }` | `when a -> stmt; when b -> stmt;` |
| `goto cleanup;` | ❌ Not available — use `when` guards |
| `while (cond) { ... }` | `while (cond) { ... }` |
| `for (int i=0; i<n; i++)` | `for (int32_t i = 0; i < n; i++)` |
| `switch(x) { case 1: ... }` | `switch(x) { case 1 -> ... }` |
| `do { ... } while (cond);` | `do { ... } while (cond);` |

## Why This Matters for Verification

Flat control flow means VRP (Value Range Propagation) can track variable
ranges through every path:

```c
[[result >= 0]
int32_t abs_value(int32_t x) {
    when x < 0 -> return -x;     // VRP: x ∈ [INT_MIN, -1] here
    return x;                      // VRP: x ∈ [0, INT_MAX] here
}
```

The VRP pass infers that on the `return x` path, `x >= 0`, which proves
`result >= 0` without needing to analyze nested branches.
