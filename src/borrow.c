// 2026-07-13 — Lexical borrow checker implementation.
//   Walks the typed AST and validates variable state transitions
//   using the 5-state machine from state.h/state.c.

#include "borrow.h"
#include "state.h"
#include "type.h"
#include <string.h>
#include <stdio.h>

// ── Forward declarations ─────────────────────────────────────────────────

static void borrow_walk(AstNode* node, SymbolTable* symtab, ErrorList* errors);

// ── Helpers ──────────────────────────────────────────────────────────────

/// Check if a token text matches a given string.
static int tok_eq(AstNode* node, const char* s) {
    if (!node || !node->token.text || !s) return 0;
    size_t slen = strlen(s);
    return node->token.len == slen &&
           memcmp(node->token.text, s, slen) == 0;
}

/// Transition a symbol's state and report errors.
static void trans_sym(Symbol* sym, int action, SourceLoc loc, ErrorList* errors) {
    if (!sym) return;
    VariableState new_state;
    TransitionResult r = state_transition(sym->state, action, sym->borrow_count, &new_state);
    if (r != TRANS_OK) {
        char msg[256];
        const char* action_name = "";
        switch (action) {
            case STATE_ACTION_READ:      action_name = "read"; break;
            case STATE_ACTION_WRITE:     action_name = "write"; break;
            case STATE_ACTION_MOVE:      action_name = "move"; break;
            case STATE_ACTION_DROP:      action_name = "drop"; break;
            case STATE_ACTION_BORROW:    action_name = "borrow"; break;
            case STATE_ACTION_BORROW_END: action_name = "borrow_end"; break;
        }
        snprintf(msg, sizeof(msg), "cannot %s '%s': %s",
                 action_name, sym->name, state_transition_name(r));
        errlist_add(errors, ERROR_LEVEL_ERROR, loc, "%s", msg);
    } else {
        sym->state = new_state;
        // Update borrow count
        if (action == STATE_ACTION_BORROW) sym->borrow_count++;
        if (action == STATE_ACTION_BORROW_END && sym->borrow_count > 0) sym->borrow_count--;
    }
}

/// Find a symbol for a variable reference node.
static Symbol* var_sym(AstNode* node, SymbolTable* symtab) {
    if (node->kind == NODE_VARIABLE && node->symbol) {
        return node->symbol;
    }
    // Fallback: look up by name
    if (node->kind == NODE_VARIABLE) {
        char name[256];
        int nlen = (int)node->token.len;
        if (nlen > 255) nlen = 255;
        snprintf(name, sizeof(name), "%.*s", nlen, node->token.text);
        return symtab_lookup(symtab, name);
    }
    return NULL;
}

// ── Main borrow walk ─────────────────────────────────────────────────────

static void borrow_walk(AstNode* node, SymbolTable* symtab, ErrorList* errors) {
    if (!node) return;

    switch (node->kind) {

    case NODE_VARIABLE: {
        // Variable in expression context (read unless handled by parent)
        // The parent (ASSIGN, CALL, etc.) handles its own transitions
        // For standalone variable references, it's a read
        Symbol* sym = var_sym(node, symtab);
        if (sym) {
            trans_sym(sym, STATE_ACTION_READ, node->token.loc, errors);
        }
        break;
    }

    case NODE_ASSIGN: {
        if (node->child_count < 2) break;
        AstNode* dest = node->children[0];
        AstNode* src = node->children[1];

        // Write to destination
        if (dest->kind == NODE_VARIABLE) {
            Symbol* sym = var_sym(dest, symtab);
            if (sym) {
                trans_sym(sym, STATE_ACTION_WRITE, dest->token.loc, errors);
            }
        }

        // Read from source — walk recursively
        borrow_walk(src, symtab, errors);
        break;
    }

    case NODE_DECL: {
        // Variable declaration with initializer
        // If it has an init, it's a write to the declared variable
        if (node->child_count >= 2) {
            char name[256];
            int nlen = (int)node->token.len;
            if (nlen > 255) nlen = 255;
            snprintf(name, sizeof(name), "%.*s", nlen, node->token.text);
            Symbol* sym = symtab_lookup(symtab, name);
            if (sym && node->child_count > 1) {
                // Has initializer — write
                trans_sym(sym, STATE_ACTION_WRITE, node->token.loc, errors);
            }
        }
        // Walk initializer expressions for variable references
        for (size_t i = 2; i < node->child_count; i++) {
            if (node->children[i]->kind != NODE_ARRAY_SUB) {
                borrow_walk(node->children[i], symtab, errors);
            }
        }
        break;
    }

    case NODE_CALL: {
        if (node->child_count < 1) break;
        // Check if it's a `free()` call
        AstNode* callee = node->children[0];
        if (callee->kind == NODE_VARIABLE && tok_eq(callee, "free")) {
            // First argument is the variable to drop
            if (node->child_count > 1) {
                AstNode* arg = node->children[1];
                if (arg->kind == NODE_VARIABLE) {
                    Symbol* sym = var_sym(arg, symtab);
                    if (sym) {
                        trans_sym(sym, STATE_ACTION_DROP, arg->token.loc, errors);
                    }
                }
            }
        } else {
            // Regular function call: walk arguments for reads
            for (size_t i = 1; i < node->child_count; i++) {
                borrow_walk(node->children[i], symtab, errors);
            }
        }
        break;
    }

    case NODE_RETURN: {
        // Walking the return expression handles variable reads
        for (size_t i = 0; i < node->child_count; i++) {
            borrow_walk(node->children[i], symtab, errors);
        }
        break;
    }

    case NODE_BLOCK: {
        // Track borrow references that go out of scope
        // Walk children first
        for (size_t i = 0; i < node->child_count; i++) {
            borrow_walk(node->children[i], symtab, errors);
        }
        // At block exit, end all borrows for variables in this scope
        // (handled by scope pop — scan current scope for BORROWED variables)
        break;
    }

    case NODE_FUNCTION: {
        // Find the body and walk it
        AstNode* body = NULL;
        for (size_t i = 0; i < node->child_count; i++) {
            if (node->children[i]->kind == NODE_BLOCK) {
                body = node->children[i];
                break;
            }
        }
        if (body) {
            borrow_walk(body, symtab, errors);
        }
        break;
    }

    default:
        // Walk children for other nodes
        for (size_t i = 0; i < node->child_count; i++) {
            borrow_walk(node->children[i], symtab, errors);
        }
        break;
    }
}

// ── Public API ───────────────────────────────────────────────────────────

int borrow_check(AstNode* root, SymbolTable* symtab, ErrorList* errors) {
    if (!root || !symtab) return -1;
    borrow_walk(root, symtab, errors);
    return errors->has_errors ? 1 : 0;
}
