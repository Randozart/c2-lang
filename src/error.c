// 2026-07-13 — Error reporting infrastructure.
//   Manages a structured error list with source locations,
//   diagnostic levels, and formatted messages.

#include "error.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ── Initial capacity ────────────────────────────────────────────────────

#define INITIAL_ERROR_CAPACITY 16

// ── Create / destroy ────────────────────────────────────────────────────

ErrorList* errlist_create(void) {
    ErrorList* el = (ErrorList*)calloc(1, sizeof(ErrorList));
    if (!el) return NULL;
    el->capacity = INITIAL_ERROR_CAPACITY;
    el->items = (ErrorMessage*)calloc(el->capacity, sizeof(ErrorMessage));
    if (!el->items) {
        free(el);
        return NULL;
    }
    el->count = 0;
    el->has_errors = 0;
    return el;
}

void errlist_destroy(ErrorList* el) {
    if (!el) return;
    for (size_t i = 0; i < el->count; i++) {
        free(el->items[i].detail);
    }
    free(el->items);
    free(el);
}

// ── Adding errors ───────────────────────────────────────────────────────

void errlist_add(ErrorList* el, ErrorLevel level, SourceLoc loc,
                 const char* fmt, ...) {
    if (!el) return;

    if (el->count >= el->capacity) {
        el->capacity *= 2;
        el->items = (ErrorMessage*)realloc(el->items,
            el->capacity * sizeof(ErrorMessage));
    }

    ErrorMessage* msg = &el->items[el->count];
    msg->level = level;
    msg->loc = loc;
    msg->filename = loc.filename;
    msg->detail = NULL;

    // Format the message
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed >= 0) {
        char* buf = (char*)malloc((size_t)needed + 1);
        if (buf) {
            va_start(args, fmt);
            vsnprintf(buf, (size_t)needed + 1, fmt, args);
            va_end(args);
            msg->message = buf;
        }
    }

    if (level >= ERROR_LEVEL_ERROR) {
        el->has_errors = 1;
    }

    el->count++;
}

void errlist_add_note(ErrorList* el, SourceLoc loc, const char* fmt, ...) {
    (void)loc;
    if (!el || el->count == 0) return;

    ErrorMessage* msg = &el->items[el->count - 1];

    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed >= 0) {
        char* buf = (char*)malloc((size_t)needed + 1);
        if (buf) {
            va_start(args, fmt);
            vsnprintf(buf, (size_t)needed + 1, fmt, args);
            va_end(args);

            // Append to existing detail
            size_t old_len = msg->detail ? strlen(msg->detail) : 0;
            size_t new_len = old_len + strlen(buf) + 3;
            char* new_detail = (char*)realloc(msg->detail, new_len);
            if (new_detail) {
                if (msg->detail) {
                    snprintf(new_detail + old_len, new_len - old_len, "\n  %s", buf);
                } else {
                    snprintf(new_detail, new_len, "  %s", buf);
                }
                msg->detail = new_detail;
            }
            free(buf);
        }
    }
}

// ── Printing errors ─────────────────────────────────────────────────────

static const char* level_name(ErrorLevel level) {
    switch (level) {
        case ERROR_LEVEL_NOTE:    return "note";
        case ERROR_LEVEL_WARNING: return "warning";
        case ERROR_LEVEL_ERROR:   return "error";
        case ERROR_LEVEL_FATAL:   return "fatal";
    }
    return "unknown";
}

void errlist_print(ErrorList* el) {
    if (!el) return;

    for (size_t i = 0; i < el->count; i++) {
        ErrorMessage* msg = &el->items[i];

        fprintf(stderr, "%s:%zu:%zu: %s: %s\n",
                msg->filename ? msg->filename : "<unknown>",
                msg->loc.line, msg->loc.col,
                level_name(msg->level),
                msg->message);

        if (msg->detail) {
            fprintf(stderr, "  %s\n", msg->detail);
        }
    }
}

// ── Utilities ───────────────────────────────────────────────────────────

size_t errlist_count(ErrorList* el) {
    return el ? el->count : 0;
}

void errlist_clear(ErrorList* el) {
    if (!el) return;
    for (size_t i = 0; i < el->count; i++) {
        free(el->items[i].detail);
    }
    el->count = 0;
    el->has_errors = 0;
}
