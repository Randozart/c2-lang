// 2026-07-13 — Variable state machine for C² lexical borrow checking.
//   Defines the 5-state lifecycle enum and transition validation functions.
//   Used by the borrow checker (borrow.c) and drop injector (drop.c).

#ifndef C2_STATE_H
#define C2_STATE_H

#include <stddef.h>

// ── Variable lifecycle states ───────────────────────────────────────────

typedef enum {
    STATE_UNINITIALIZED = 0,
    STATE_OWNED,        // Variable owns the resource
    STATE_BORROWED,     // Variable is temporarily borrowed (read-only)
    STATE_MOVED,        // Ownership was transferred to another owner
    STATE_DROPPED,      // Resource was cleaned up (drop called)
} VariableState;

// ── Transition results ──────────────────────────────────────────────────

typedef enum {
    TRANS_OK,
    TRANS_ERROR_USE_AFTER_MOVE,
    TRANS_ERROR_USE_AFTER_DROP,
    TRANS_ERROR_MUTATE_WHILE_BORROWED,
    TRANS_ERROR_MOVE_WHILE_BORROWED,
    TRANS_ERROR_DROP_WHILE_BORROWED,
    TRANS_ERROR_DOUBLE_DROP,
    TRANS_ERROR_UNINITIALIZED_READ,
    TRANS_ERROR_NOT_OWNER,
} TransitionResult;

// ── Transition validation ───────────────────────────────────────────────

/// Validate a transition from `from` state with the given action.
/// `borrow_count` is the current number of active borrows.
/// Returns the result code and sets `*out_new_state` to the resulting state
/// (valid only when the result is TRANS_OK).
TransitionResult state_transition(
    VariableState from,
    int           action,     // 0 = read, 1 = write, 2 = move, 3 = drop, 4 = borrow, 5 = borrow_end
    size_t        borrow_count,
    VariableState* out_new_state
);

// ── Action constants ────────────────────────────────────────────────────

#define STATE_ACTION_READ       0
#define STATE_ACTION_WRITE      1
#define STATE_ACTION_MOVE       2
#define STATE_ACTION_DROP       3
#define STATE_ACTION_BORROW     4
#define STATE_ACTION_BORROW_END 5

// ── Utility ─────────────────────────────────────────────────────────────

/// Return a human-readable string for a state value.
const char* state_name(VariableState s);

/// Return a human-readable string for a transition result.
const char* state_transition_name(TransitionResult r);

/// Return true if the state is considered "active" (owns a resource).
/// Active states: OWNED, BORROWED.
int state_is_active(VariableState s);

#endif // C2_STATE_H
