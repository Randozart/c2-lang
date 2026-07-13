// 2026-07-13 — AST node construction and manipulation functions.
//   Implements the allocation, freeing, and traversal APIs declared in ast.h.

#include "ast.h"
#include "type.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ── Node allocation ─────────────────────────────────────────────────────

AstNode* ast_alloc_node(NodeKind kind, Token token) {
    AstNode* node = (AstNode*)calloc(1, sizeof(AstNode));
    if (!node) return NULL;
    node->kind = kind;
    node->token = token;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    node->type = NULL;
    node->symbol = NULL;
    node->range.lo = 0;
    node->range.hi = 0;
    node->range.has_range = 0;
    node->flags = 0;
    return node;
}

void ast_free_node(AstNode* node) {
    if (!node) return;
    free(node->children);
    if (node->type) type_free(node->type);
    free(node);
}

void ast_free_tree(AstNode* root) {
    if (!root) return;
    for (size_t i = 0; i < root->child_count; i++) {
        ast_free_tree(root->children[i]);
    }
    ast_free_node(root);
}

// ── Child management ────────────────────────────────────────────────────

void ast_add_child(AstNode* parent, AstNode* child) {
    if (!parent || !child) return;
    if (parent->child_count >= parent->child_capacity) {
        size_t new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        AstNode** new_children = (AstNode**)realloc(
            parent->children, new_cap * sizeof(AstNode*));
        if (!new_children) return;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }
    parent->children[parent->child_count++] = child;
}

// ── Convenience constructors ────────────────────────────────────────────

AstNode* ast_make_literal_int(int64_t value, SourceLoc loc) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = TOK_INT_LITERAL;
    t.loc = loc;
    t.value.i64 = value;
    AstNode* node = ast_alloc_node(NODE_LITERAL_INT, t);
    return node;
}

AstNode* ast_make_variable(Token name_tok) {
    AstNode* node = ast_alloc_node(NODE_VARIABLE, name_tok);
    return node;
}

AstNode* ast_make_binary_op(int op_kind, AstNode* left, AstNode* right, SourceLoc loc) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = (TokenKind)op_kind;
    t.loc = loc;
    AstNode* node = ast_alloc_node(NODE_BINARY_OP, t);
    ast_add_child(node, left);
    ast_add_child(node, right);
    return node;
}

AstNode* ast_make_function(Token name_tok, AstNode* ret_type,
                           AstNode* params, AstNode* body,
                           AstNode* pre, AstNode* post, AstNode* deriv) {
    AstNode* node = ast_alloc_node(NODE_FUNCTION, name_tok);

    if (ret_type) ast_add_child(node, ret_type);
    if (pre) {
        AstNode* pre_node = ast_alloc_node(NODE_CONTRACT_PRE, pre->token);
        ast_add_child(pre_node, pre);
        ast_add_child(node, pre_node);
    }
    if (post) {
        AstNode* post_node = ast_alloc_node(NODE_CONTRACT_POST, post->token);
        ast_add_child(post_node, post);
        ast_add_child(node, post_node);
    }
    if (params) ast_add_child(node, params);
    if (body) ast_add_child(node, body);
    if (deriv) ast_add_child(node, deriv);

    return node;
}

AstNode* ast_make_block(SourceLoc loc) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = TOK_LBRACE;
    t.loc = loc;
    return ast_alloc_node(NODE_BLOCK, t);
}

AstNode* ast_make_when(AstNode* cond, AstNode* stmt, SourceLoc loc) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = TOK_WHEN;
    t.loc = loc;
    AstNode* node = ast_alloc_node(NODE_WHEN, t);
    if (cond) ast_add_child(node, cond);
    if (stmt) ast_add_child(node, stmt);
    return node;
}

// ── AST traversal ───────────────────────────────────────────────────────

int ast_walk_preorder(AstNode* root, AstVisitor visitor, void* user_data) {
    if (!root) return 0;
    int result = visitor(root, user_data);
    if (result != 0) return result;
    for (size_t i = 0; i < root->child_count; i++) {
        result = ast_walk_preorder(root->children[i], visitor, user_data);
        if (result != 0) return result;
    }
    return 0;
}

int ast_walk_postorder(AstNode* root, AstVisitor visitor, void* user_data) {
    if (!root) return 0;
    for (size_t i = 0; i < root->child_count; i++) {
        int result = ast_walk_postorder(root->children[i], visitor, user_data);
        if (result != 0) return result;
    }
    return visitor(root, user_data);
}
