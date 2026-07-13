# 03 — Ownership

## The Problem C² Solves

In C, memory management is manual. You decide when to `malloc` and `free`.
This leads to three classic bugs:

```c
// Use-after-free
free(ptr);
*ptr = 42;  // 💥

// Double-free
free(ptr);
free(ptr);  // 💥

// Memory leak
ptr = malloc(100);
// forgot to free(ptr)  // 💧
```

C² eliminates all three at compile time using a **lexical ownership model**:

- Every variable has a state that the compiler tracks
- State transitions are validated by the borrow checker pass
- Destructors are automatically injected at scope boundaries
- Early `free()` is recognized and suppresses the automatic drop

## Variable States

Every variable starts in `UNINITIALIZED` and moves through a 5-state lifecycle:

```
UNINITIALIZED ──write──→ OWNED ──read──→ OWNED
                    │       │──write──→ OWNED
                    │       │──move──→ MOVED ──write──→ OWNED
                    │       │──drop──→ DROPPED
                    │       │──borrow──→ BORROWED
                    │       └──borrow_end──→ OWNED
                    │
                    └──borrow──→ BORROWED
```

| State | Meaning |
|-------|---------|
| `UNINITIALIZED` | Declared but not yet written to |
| `OWNED` | Holds a valid value, responsible for cleanup |
| `BORROWED` | Being read via a borrow reference — no mutation allowed |
| `MOVED` | Ownership transferred elsewhere — reading is an error |
| `DROPPED` | Already freed — reading or double-free is an error |

## `borrow` vs `own`

Function parameters use **qualifiers** to declare ownership intent:

```c
// C: intent unclear
void process(int32_t* p);

// C²: intent is compiler-checked
void read(borrow int32_t* p);     // I only need to read it
void write(own int32_t* p);       // I need to modify it
```

These are checked at the call site:

```c
void example() {
    int32_t x = 42;
    read(&x);   // OK: borrow is read-only, x is still OWNED
    write(&x);  // OK: x is OWNED, but now it's... wait
}
```

Actually, passing `&x` to `own` transfers ownership — but `x` is still
in scope! The borrow checker catches this:

```c
void example() {
    int32_t x = 42;
    write(&x);  // Error: cannot move x while it's in scope
}
```

For the common case of modifying a variable through a pointer, use `borrow`:

```c
void inc(borrow int32_t* p) { *p = *p + 1; }  // Error: cannot write through borrow!
```

If you need to modify through a pointer, use `own`:

```c
void inc(own int32_t* p) { *p = *p + 1; }

void example() {
    int32_t x = 42;
    inc(&x);
    // x is now OWNED again — inc returned ownership
}
```

## `free()` and Early Drop

Calling `free(ptr)` transitions the variable from `OWNED` to `DROPPED`.
This suppresses the automatic drop injection at scope exit.

```c
void example() {
    own int32_t* data = malloc(100);
    // ... use data ...
    free(data);     // OWNED → DROPPED
    free(data);     // Error: double-drop (DROPPED → DROPPED invalid)
    *data = 42;     // Error: use-after-drop
}
```

If you don't call `free()`, the drop injector adds a destructor call
for you at the end of the scope:

```c
void example() {
    own int32_t* data = malloc(100);
    // ... use data ...
    // ← compiler injects: free(data);
}
```

## What the Generated C Looks Like

```c
// C² source:
[1][1]
void process(borrow int32_t* p, own int32_t* q) {
    int32_t x = *p;
    *q = x + 1;
}

// Emitted C:
void process(const int32_t* restrict p, int32_t* restrict q) {
    int32_t x = *p;
    *q = x + 1;
}
```

- `borrow int32_t*` → `const int32_t* restrict` (read-only, no aliasing)
- `own int32_t*` → `int32_t* restrict` (mutable, no aliasing)
- `const` tells the C compiler the pointee data won't change
- `restrict` tells the C compiler the pointers don't alias (enables vectorization)

## Common Errors

| Error | Meaning |
|-------|---------|
| `cannot read 'x': uninitialized-read` | Used a variable before assigning it |
| `cannot read 'x': use-after-move` | Read a variable after moving its value |
| `cannot read 'x': use-after-drop` | Read a variable after freeing it |
| `cannot write 'x': mutate-while-borrowed` | Modified a borrowed variable |
| `cannot drop 'x': double-drop` | Called free() twice on the same pointer |
| `cannot drop 'x': drop-while-borrowed` | Freed a pointer while borrows exist |

## Behind the Scenes

The borrow checker (Phase E) is a separate compiler pass that runs after
typechecking and VRP. It walks the AST and calls `state_transition()` for
each variable access. The drop injector (also Phase E) then walks the AST
again, looking for variables still in `OWNED` state at scope boundaries,
and inserts `NODE_DROP_CALL` nodes.

The codegen pass emits these as `free()` calls in the generated C, placed
just before the closing brace of each scope.
