// 2026-07-14 — Fixed symtab_lookup_current → symtab_lookup for
//   dead scope search. Only emit drop calls for pointer types.
// 2026-07-13 — Drop injection pass implementation.
//   Inserts NODE_DROP_CALL nodes at block scope boundaries for
//   owned variables. Skips variables already transitioned to
//   STATE_DROPPED (early free via free() call).

#include "drop.h"
#include "state.h"
#include "type.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ── Owned variable tracker ───────────────────────────────────────────────
// Simple dynamic array of symbols currently in OWNED state.

typedef struct {
    Symbol** syms;
    size_t   count;
    size_t   cap;
} OwnedList;

static void owned_init(OwnedList* ol) {
    ol->syms = NULL;
    ol->count = 0;
    ol->cap = 0;
}

static void owned_push(OwnedList* ol, Symbol* sym) {
    if (ol->count >= ol->cap) {
        size_t new_cap = ol->cap == 0 ? 16 : ol->cap * 2;
        Symbol** news = (Symbol**)realloc(ol->syms, new_cap * sizeof(Symbol*));
        if (!news) return;
        ol->syms = news;
        ol->cap = new_cap;
    }
    ol->syms[ol->count++] = sym;
}

static void owned_free(OwnedList* ol) {
    free(ol->syms);
    ol->syms = NULL;
    ol->count = 0;
    ol->cap = 0;
}

// ── Forward declarations ─────────────────────────────────────────────────

static void drop_walk(AstNode* node, SymbolTable* symtab, OwnedList* owned,
                      ErrorList* errors);

// ── Helpers ──────────────────────────────────────────────────────────────

static int var_is_owned(Symbol* sym) {
    return sym && sym->state == STATE_OWNED;
}

/// Create a NODE_DROP_CALL for the given variable.
static AstNode* make_drop_call(Symbol* sym, SourceLoc loc) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = TOK_IDENTIFIER;
    t.loc = loc;
    t.text = sym->name;
    t.len = strlen(sym->name);
    AstNode* node = ast_alloc_node(NODE_DROP_CALL, t);
    node->symbol = sym;
    return node;
}

// ── Main drop walk ───────────────────────────────────────────────────────

static void drop_walk(AstNode* node, SymbolTable* symtab, OwnedList* owned,
                      ErrorList* errors) {
    if (!node) return;
    (void)errors;

    switch (node->kind) {

    case NODE_DECL: {
        char name[256];
        int nlen = (int)node->token.len;
        if (nlen > 255) nlen = 255;
        snprintf(name, sizeof(name), "%.*s", nlen, node->token.text);
        Symbol* sym = symtab_lookup(symtab, name);
        if (sym && var_is_owned(sym)) {
            int found = 0;
            for (size_t i = 0; i < owned->count; i++) {
                if (owned->syms[i] == sym) { found = 1; break; }
            }
            if (!found) owned_push(owned, sym);
        }
        break;
    }

    case NODE_FUNCTION: {
        // Process function body
        AstNode* body = NULL;
        for (size_t i = 0; i < node->child_count; i++) {
            if (node->children[i]->kind == NODE_BLOCK) {
                body = node->children[i];
                break;
            }
        }
        if (body) {
            OwnedList func_owned;
            owned_init(&func_owned);
            // Copy current owned list
            for (size_t i = 0; i < owned->count; i++) {
                owned_push(&func_owned, owned->syms[i]);
            }
            drop_walk(body, symtab, &func_owned, errors);
            // At function exit, add drop calls for still-owned pointer variables
            for (size_t i = 0; i < func_owned.count; i++) {
                Symbol* fsym = func_owned.syms[i];
                if (var_is_owned(fsym)) {
                    if (fsym->type && type_is_pointer(fsym->type)) {
                        AstNode* dc = make_drop_call(fsym, node->token.loc);
                        ast_add_child(node, dc);
                    }
                }
            }
            owned_free(&func_owned);
        }
        break;
    }

    case NODE_BLOCK: {
        OwnedList block_owned;
        owned_init(&block_owned);
        size_t start_count = owned->count;

        for (size_t i = 0; i < node->child_count; i++) {
            drop_walk(node->children[i], symtab, owned, errors);
        }

        for (size_t i = start_count; i < owned->count; i++) {
            Symbol* sym = owned->syms[i];
            if (var_is_owned(sym)) {
                // Only inject drop for pointer types (primitives live on stack)
                if (sym->type && type_is_pointer(sym->type)) {
                    AstNode* dc = make_drop_call(sym, node->token.loc);
                    ast_add_child(node, dc);
                }
            }
        }

        owned_free(&block_owned);
        break;
    }

    case NODE_ASSIGN: {
        if (node->child_count >= 2 && node->children[0]->kind == NODE_VARIABLE) {
            Symbol* sym = node->children[0]->symbol;
            if (sym && var_is_owned(sym)) {
                int found = 0;
                for (size_t i = 0; i < owned->count; i++) {
                    if (owned->syms[i] == sym) { found = 1; break; }
                }
                if (!found) owned_push(owned, sym);
            }
        }
        // Walk children
        for (size_t i = 0; i < node->child_count; i++) {
            drop_walk(node->children[i], symtab, owned, errors);
        }
        break;
    }

    default:
        for (size_t i = 0; i < node->child_count; i++) {
            drop_walk(node->children[i], symtab, owned, errors);
        }
        break;
    }
}

// ── Public API ───────────────────────────────────────────────────────────

int drop_inject(AstNode* root, SymbolTable* symtab, ErrorList* errors) {
    if (!root || !symtab) return -1;
    OwnedList global_owned;
    owned_init(&global_owned);
    drop_walk(root, symtab, &global_owned, errors);
    owned_free(&global_owned);
    return 0;
}
