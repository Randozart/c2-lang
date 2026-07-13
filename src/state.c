// 2026-07-13 — Variable state machine implementation for C².
//   Implements the 5-state lifecycle transition validation.

#include "state.h"
#include <stdio.h>

TransitionResult state_transition(
    VariableState from,
    int           action,
    size_t        borrow_count,
    VariableState* out_new_state
) {
    if (!out_new_state) return TRANS_ERROR_NOT_OWNER;

    switch (from) {
    case STATE_UNINITIALIZED:
        if (action == STATE_ACTION_READ) {
            *out_new_state = STATE_UNINITIALIZED;
            return TRANS_ERROR_UNINITIALIZED_READ;
        }
        if (action == STATE_ACTION_WRITE) {
            *out_new_state = STATE_OWNED;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_BORROW) {
            *out_new_state = STATE_BORROWED;
            return TRANS_OK;
        }
        *out_new_state = STATE_UNINITIALIZED;
        return TRANS_ERROR_NOT_OWNER;

    case STATE_OWNED:
        if (action == STATE_ACTION_READ) {
            *out_new_state = STATE_OWNED;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_WRITE) {
            *out_new_state = STATE_OWNED;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_MOVE) {
            *out_new_state = STATE_MOVED;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_DROP) {
            *out_new_state = STATE_DROPPED;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_BORROW) {
            *out_new_state = STATE_BORROWED;
            return TRANS_OK;
        }
        *out_new_state = STATE_OWNED;
        return TRANS_ERROR_NOT_OWNER;

    case STATE_BORROWED:
        if (action == STATE_ACTION_READ) {
            *out_new_state = STATE_BORROWED;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_BORROW_END) {
            VariableState next = (borrow_count <= 1) ? STATE_OWNED : STATE_BORROWED;
            *out_new_state = next;
            return TRANS_OK;
        }
        if (action == STATE_ACTION_WRITE) {
            *out_new_state = STATE_BORROWED;
            return TRANS_ERROR_MUTATE_WHILE_BORROWED;
        }
        if (action == STATE_ACTION_MOVE) {
            *out_new_state = STATE_BORROWED;
            return TRANS_ERROR_MOVE_WHILE_BORROWED;
        }
        if (action == STATE_ACTION_DROP) {
            *out_new_state = STATE_BORROWED;
            return TRANS_ERROR_DROP_WHILE_BORROWED;
        }
        *out_new_state = STATE_BORROWED;
        return TRANS_ERROR_NOT_OWNER;

    case STATE_MOVED:
        if (action == STATE_ACTION_READ) {
            *out_new_state = STATE_MOVED;
            return TRANS_ERROR_USE_AFTER_MOVE;
        }
        if (action == STATE_ACTION_WRITE) {
            *out_new_state = STATE_OWNED;
            return TRANS_OK;
        }
        *out_new_state = STATE_MOVED;
        return TRANS_ERROR_NOT_OWNER;

    case STATE_DROPPED:
        if (action == STATE_ACTION_DROP) {
            *out_new_state = STATE_DROPPED;
            return TRANS_ERROR_DOUBLE_DROP;
        }
        *out_new_state = STATE_DROPPED;
        return TRANS_ERROR_USE_AFTER_DROP;

    default:
        *out_new_state = STATE_UNINITIALIZED;
        return TRANS_ERROR_NOT_OWNER;
    }
}

const char* state_name(VariableState s) {
    switch (s) {
    case STATE_UNINITIALIZED: return "uninitialized";
    case STATE_OWNED:         return "owned";
    case STATE_BORROWED:      return "borrowed";
    case STATE_MOVED:         return "moved";
    case STATE_DROPPED:       return "dropped";
    default:                  return "<unknown>";
    }
}

const char* state_transition_name(TransitionResult r) {
    switch (r) {
    case TRANS_OK:                       return "ok";
    case TRANS_ERROR_USE_AFTER_MOVE:     return "use-after-move";
    case TRANS_ERROR_USE_AFTER_DROP:     return "use-after-drop";
    case TRANS_ERROR_MUTATE_WHILE_BORROWED: return "mutate-while-borrowed";
    case TRANS_ERROR_MOVE_WHILE_BORROWED:   return "move-while-borrowed";
    case TRANS_ERROR_DROP_WHILE_BORROWED:   return "drop-while-borrowed";
    case TRANS_ERROR_DOUBLE_DROP:        return "double-drop";
    case TRANS_ERROR_UNINITIALIZED_READ: return "uninitialized-read";
    case TRANS_ERROR_NOT_OWNER:          return "not-owner";
    default:                             return "<unknown>";
    }
}

int state_is_active(VariableState s) {
    return s == STATE_OWNED || s == STATE_BORROWED;
}
