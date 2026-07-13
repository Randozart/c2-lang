// 2026-07-13 — Symbol table and scope stack for C².
//   Each Symbol stores name, type, variable state, borrow count,
//   and a reference to the drop function if applicable.
//   The scope stack is a linked list of hash tables.

#ifndef C2_SYMBOL_H
#define C2_SYMBOL_H

#include <stddef.h>
#include "ast.h"
#include "state.h"

// ── Forward declarations ────────────────────────────────────────────────

typedef struct Symbol Symbol;
typedef struct Scope Scope;

// ── Symbol ──────────────────────────────────────────────────────────────

struct Symbol {
    char*          name;
    Type*          type;
    VariableState  state;
    size_t         borrow_count;

    // Drop function (set if the type has [[c2::drop(fn)]])
    // Stored as the AST node for the cleanup call.
    AstNode*       drop_fn_call;

    // Source location of the declaration (for error reporting)
    SourceLoc      decl_loc;

    // Scope depth (for diagnostics)
    size_t         scope_depth;

    // Inferred value range (populated by VRP pass)
    ValueRange     range;

    // Linked list chain (for the scope's hash table bucket)
    Symbol*        next;
};

// ── Scope (stack frame) ─────────────────────────────────────────────────

struct Scope {
    Scope*   parent;
    size_t   depth;
    Symbol** buckets;
    size_t   bucket_count;
    size_t   symbol_count;
};

// ── Symbol table (top-level) ────────────────────────────────────────────

typedef struct {
    Scope* current;
    Scope* global;
    Scope* dead_scopes; // Popped scopes kept alive for node->symbol refs
    size_t num_scopes;
} SymbolTable;

// ── Symbol table operations ─────────────────────────────────────────────

SymbolTable* symtab_create(void);
void         symtab_destroy(SymbolTable* st);

/// Push a new scope. Returns the new scope's depth (0-indexed).
size_t symtab_push_scope(SymbolTable* st);

/// Pop the current scope. Returns the popped scope's depth.
size_t symtab_pop_scope(SymbolTable* st);

/// Look up a symbol by name. Searches from current scope upward.
/// Returns NULL if not found.
Symbol* symtab_lookup(SymbolTable* st, const char* name);

/// Look up a symbol in the current scope only.
Symbol* symtab_lookup_current(SymbolTable* st, const char* name);

/// Insert a symbol into the current scope.
/// Returns the new symbol, or NULL if a symbol with the same name
/// already exists in the current scope.
Symbol* symtab_insert(SymbolTable* st, const char* name, Type* type, SourceLoc loc);

/// Set a symbol's variable state with validation.
/// Returns TRANS_OK on success, or an error code on invalid transition.
TransitionResult symtab_transition(SymbolTable* st, Symbol* sym, int action);

/// Iterate over all symbols in the current scope.
/// Calls `callback` for each symbol. If callback returns non-zero, stop.
void symtab_for_each_in_scope(SymbolTable* st, int (*callback)(Symbol* sym, void* user), void* user);

#endif // C2_SYMBOL_H
