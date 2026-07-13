// 2026-07-13 — Symbol table and scope stack implementation for C².
//   Hash-table-backed scope chain with push/pop.

#include "symbol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SYMTAB_BUCKETS 64

static size_t hash_str(const char* s) {
    size_t h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) h = ((h << 5) + h) + (size_t)c;
    return h;
}

static Symbol* symbol_create(const char* name, Type* type, SourceLoc loc, size_t depth) {
    Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
    if (!sym) return NULL;
    sym->name = strdup(name);
    sym->type = type;
    sym->decl_loc = loc;
    sym->scope_depth = depth;
    sym->state = STATE_UNINITIALIZED;
    return sym;
}

static void symbol_destroy(Symbol* sym) {
    if (!sym) return;
    free(sym->name);
    free(sym);
}

static Scope* scope_create(Scope* parent, size_t depth) {
    Scope* s = (Scope*)calloc(1, sizeof(Scope));
    if (!s) return NULL;
    s->parent = parent;
    s->depth = depth;
    s->bucket_count = SYMTAB_BUCKETS;
    s->buckets = (Symbol**)calloc(s->bucket_count, sizeof(Symbol*));
    if (!s->buckets) { free(s); return NULL; }
    return s;
}

static void scope_destroy(Scope* s) {
    if (!s) return;
    for (size_t i = 0; i < s->bucket_count; i++) {
        Symbol* cur = s->buckets[i];
        while (cur) {
            Symbol* next = cur->next;
            symbol_destroy(cur);
            cur = next;
        }
    }
    free(s->buckets);
    free(s);
}

// ── Public API ───────────────────────────────────────────────────────────

SymbolTable* symtab_create(void) {
    SymbolTable* st = (SymbolTable*)calloc(1, sizeof(SymbolTable));
    if (!st) return NULL;
    st->global = scope_create(NULL, 0);
    st->current = st->global;
    st->num_scopes = 1;
    return st;
}

void symtab_destroy(SymbolTable* st) {
    if (!st) return;
    // Pop and destroy all scopes
    while (st->current && st->current != st->global) {
        Scope* parent = st->current->parent;
        scope_destroy(st->current);
        st->current = parent;
    }
    scope_destroy(st->global);
    free(st);
}

size_t symtab_push_scope(SymbolTable* st) {
    if (!st) return 0;
    st->current = scope_create(st->current, st->num_scopes);
    st->num_scopes++;
    return st->num_scopes - 1;
}

size_t symtab_pop_scope(SymbolTable* st) {
    if (!st || !st->current || !st->current->parent) return 0;
    size_t depth = st->current->depth;
    Scope* parent = st->current->parent;
    scope_destroy(st->current);
    st->current = parent;
    return depth;
}

Symbol* symtab_lookup(SymbolTable* st, const char* name) {
    if (!st || !name) return NULL;
    Scope* s = st->current;
    while (s) {
        size_t idx = hash_str(name) % s->bucket_count;
        Symbol* cur = s->buckets[idx];
        while (cur) {
            if (strcmp(cur->name, name) == 0) return cur;
            cur = cur->next;
        }
        s = s->parent;
    }
    return NULL;
}

Symbol* symtab_lookup_current(SymbolTable* st, const char* name) {
    if (!st || !st->current || !name) return NULL;
    size_t idx = hash_str(name) % st->current->bucket_count;
    Symbol* cur = st->current->buckets[idx];
    while (cur) {
        if (strcmp(cur->name, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

Symbol* symtab_insert(SymbolTable* st, const char* name, Type* type, SourceLoc loc) {
    if (!st || !st->current || !name) return NULL;

    // Check for duplicate in current scope
    if (symtab_lookup_current(st, name)) return NULL;

    size_t idx = hash_str(name) % st->current->bucket_count;
    Symbol* sym = symbol_create(name, type, loc, st->current->depth);
    if (!sym) return NULL;

    sym->next = st->current->buckets[idx];
    st->current->buckets[idx] = sym;
    st->current->symbol_count++;
    return sym;
}

TransitionResult symtab_transition(SymbolTable* st, Symbol* sym, int action) {
    if (!st || !sym) return TRANS_ERROR_NOT_OWNER;
    VariableState new_state;
    TransitionResult r = state_transition(sym->state, action, sym->borrow_count, &new_state);
    if (r == TRANS_OK) sym->state = new_state;
    return r;
}

void symtab_for_each_in_scope(SymbolTable* st, int (*callback)(Symbol* sym, void* user), void* user) {
    if (!st || !st->current || !callback) return;
    for (size_t i = 0; i < st->current->bucket_count; i++) {
        Symbol* cur = st->current->buckets[i];
        while (cur) {
            if (callback(cur, user)) return;
            cur = cur->next;
        }
    }
}
