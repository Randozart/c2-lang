// 2026-07-13 — Error reporting infrastructure for C².
//   Provides structured error messages with source locations,
//   diagnostic levels, and a global error list.

#ifndef C2_ERROR_H
#define C2_ERROR_H

#include <stddef.h>
#include "ast.h"

// ── Error levels ────────────────────────────────────────────────────────

typedef enum {
    ERROR_LEVEL_NOTE,
    ERROR_LEVEL_WARNING,
    ERROR_LEVEL_ERROR,
    ERROR_LEVEL_FATAL,
} ErrorLevel;

// ── Error message ───────────────────────────────────────────────────────

typedef struct {
    ErrorLevel  level;
    SourceLoc   loc;
    const char* message;
    const char* filename;  // Owned copy
    char*       detail;    // Optional detailed explanation
} ErrorMessage;

// ── Error list ──────────────────────────────────────────────────────────

typedef struct {
    ErrorMessage* items;
    size_t        count;
    size_t        capacity;
    int           has_errors;     // Non-zero if any error-level or above
} ErrorList;

// ── Operations ──────────────────────────────────────────────────────────

ErrorList* errlist_create(void);
void       errlist_destroy(ErrorList* el);

/// Add an error with printf-style formatting.
void errlist_add(ErrorList* el, ErrorLevel level, SourceLoc loc,
                 const char* fmt, ...);

/// Add a note to the most recently added error.
void errlist_add_note(ErrorList* el, SourceLoc loc, const char* fmt, ...);

/// Print all errors to stderr.
void errlist_print(ErrorList* el);

/// Return the number of errors (level ERROR or FATAL).
size_t errlist_count(ErrorList* el);

/// Reset the error list.
void errlist_clear(ErrorList* el);

#endif // C2_ERROR_H
