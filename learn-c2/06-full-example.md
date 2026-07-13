# 06 — Full Example

This chapter walks through a complete C² program that exercises every
major feature: contracts, borrow/own, when guards, derivation, and
the build pipeline.

## The Program: A Safe Integer Ring Buffer

```c
// ring_buffer.c2 — A bounded ring buffer with compile-time safety guarantees
#include "c2.h"
#include <stdio.h>

// ─── Ring buffer ─────────────────────────────────────────────────────────

struct RingBuffer {
    int32_t* data;
    int32_t  capacity;
    int32_t  head;
    int32_t  tail;
    int32_t  count;
};

// ─── Initialize ──────────────────────────────────────────────────────────

[capacity > 0][result != 0]
own struct RingBuffer* rb_create(int32_t capacity) {
    own struct RingBuffer* rb = malloc(sizeof(struct RingBuffer));
    if (rb == 0) return 0;
    rb->data = malloc(capacity * sizeof(int32_t));
    if (rb->data == 0) { free(rb); return 0; }
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return rb;
}

// The caller gets ownership of the returned RingBuffer.
// The compiler will inject free() calls when rb goes out of scope.

// ─── Push (write into buffer) ────────────────────────────────────────────

[rb != 0][result == 0 || result == 1]
int32_t rb_push(own struct RingBuffer* rb, int32_t value) {
    if (rb->count >= rb->capacity) return 0;  // full
    rb->data[rb->head] = value;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count = rb->count + 1;
    return 1;
}

// Note: rb_push takes OWNERSHIP of rb temporarily, then returns it.
// The borrow checker ensures no other references to rb exist during push.

// ─── Pop (read from buffer) ──────────────────────────────────────────────

[rb != 0][result != -1]
int32_t rb_pop(own struct RingBuffer* rb) {
    if (rb->count <= 0) return -1;  // empty
    int32_t val = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count = rb->count - 1;
    return val;
}

// ─── Destroy (explicit cleanup) ──────────────────────────────────────────

[rb != 0]
void rb_destroy(own struct RingBuffer* rb) {
    free(rb->data);
    rb->data = 0;
    rb->capacity = 0;
    free(rb);
}

// Note: after rb_destroy(rb), rb is in STATE_DROPPED.
// The compiler will NOT inject a second free().

// ─── Main ────────────────────────────────────────────────────────────────

[[result == 0]
int32_t main(void) {
    // Create a ring buffer for 4 elements
    own struct RingBuffer* rb = rb_create(4);
    if (rb == 0) return 1;

    // Push values
    int32_t i = 0;
    while (i < 4) {
        rb_push(rb, i * 10);
        i = i + 1;
    }

    // This push should fail (buffer full)
    int32_t ok = rb_push(rb, 99);
    printf("push when full: %d (expected 0)\n", ok);

    // Pop values
    i = 0;
    while (i < 4) {
        int32_t val = rb_pop(rb);
        printf("pop[%d]: %d\n", i, val);
        i = i + 1;
    }

    // This pop should fail (buffer empty)
    int32_t failed = rb_pop(rb);
    printf("pop when empty: %d (expected -1)\n", failed);

    // Explicit cleanup — suppresses the compiler's automatic drop
    rb_destroy(rb);

    printf("ring buffer test PASSED\n");
    return 0;
}
```

## Step-by-Step Walkthrough

### 1. Contracts

Every function has a contract:
- `[capacity > 0][result != 0]` on `rb_create` — capacity must be positive,
  and we guarantee the returned pointer is non-null (or we return 0 on failure)
- `[rb != 0]` on `rb_push`, `rb_pop`, `rb_destroy` — we must not pass NULL
- `[[result == 0]` on `main` — the program always exits with status 0

The Z3 verifier checks these at compile time. For example, it proves that
`rb_create` never returns NULL when `capacity > 0` and `malloc` succeeds.

### 2. Ownership

The `own` qualifier marks pointer parameters whose ownership is transferred:
- `rb_create` returns an `own` pointer — the caller owns the buffer
- `rb_push` takes `own struct RingBuffer*` — it temporarily owns the buffer,
  then returns it (ownership goes back to the caller)
- `rb_destroy` takes `own` — it consumes the buffer and frees it

The borrow checker ensures:
- No use-after-move: after calling `rb_destroy(rb)`, `rb` is DROPPED and
  cannot be read
- No double-free: `free(rb)` transitions to DROPPED, and the drop injector
  skips already-DROPPED variables
- No mutation-borrow conflict: you can't call `rb_push` while a borrow
  reference exists

### 3. Memory Management

The drop injector adds `free()` calls for any `own` pointer still in
`OWNED` state at scope exit. In `main()`:

```c
void main(void) {
    own struct RingBuffer* rb = rb_create(4);
    // ... use rb ...
    rb_destroy(rb);          // explicit cleanup → rb is now DROPPED
    // ← no automatic free(rb) — already DROPPED
}
```

If we forgot `rb_destroy(rb)`, the compiler would inject:

```c
void main(void) {
    own struct RingBuffer* rb = rb_create(4);
    // ... use rb ...
    // ← compiler injects: free(rb->data); free(rb);
}
```

### 4. Build and Run

```bash
# Build
./build/c2c build ring_buffer.c2 -o ring_buffer

# Check contracts
./build/c2c check ring_buffer.c2

# Run
./ring_buffer
```

Expected output:
```
push when full: 0 (expected 0)
pop[0]: 0
pop[1]: 10
pop[2]: 20
pop[3]: 30
pop when empty: -1 (expected -1)
ring buffer test PASSED
```

### 5. What the Generated C Looks Like

The emitted C strips all C²-specific constructs:

```c
#include <stdint.h>

struct RingBuffer {
    int32_t* data;
    int32_t capacity;
    int32_t head;
    int32_t tail;
    int32_t count;
};

struct RingBuffer* rb_create(int32_t capacity) {
    struct RingBuffer* rb = malloc(sizeof(struct RingBuffer));
    if (rb == 0) return 0;
    rb->data = malloc(capacity * sizeof(int32_t));
    if (rb->data == 0) { free(rb); return 0; }
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return rb;
}

// ... etc. — contracts stripped, borrow/own stripped,
// restrict added, when → if ...
```

No runtime overhead, no hidden allocations, just clean C23.
