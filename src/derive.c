// 2026-07-13 — Program synthesis engine implementation.
//   Cost-guided enumerative search: generates expression trees up to a
//   budget, evaluates them on derivation examples, and returns the
//   Pareto frontier of (ops, error) trade-offs.

#include "derive.h"
#include "type.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <sys/stat.h>

// ── Expression tree types ───────────────────────────────────────────────

typedef enum {
    EXPR_CONST_INT,
    EXPR_CONST_FLOAT,
    EXPR_VAR,
    EXPR_UNARY,
    EXPR_BINARY,
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    int      op;     // TokenKind for UNARY/BINARY
    int64_t  ival;   // For CONST_INT
    double   fval;   // For CONST_FLOAT
    int      var_idx; // For VAR — parameter index
    struct Expr*  left;
    struct Expr*  right;
} Expr;

static Expr* expr_alloc(ExprKind kind) {
    Expr* e = (Expr*)calloc(1, sizeof(Expr));
    if (e) e->kind = kind;
    return e;
}

static Expr* expr_const_int(int64_t v) {
    Expr* e = expr_alloc(EXPR_CONST_INT);
    if (e) e->ival = v;
    return e;
}

static Expr* expr_const_float(double v) {
    Expr* e = expr_alloc(EXPR_CONST_FLOAT);
    if (e) e->fval = v;
    return e;
}

static Expr* expr_var(int idx) {
    Expr* e = expr_alloc(EXPR_VAR);
    if (e) e->var_idx = idx;
    return e;
}

static Expr* expr_unary(int op, Expr* child) {
    Expr* e = expr_alloc(EXPR_UNARY);
    if (e) { e->op = op; e->left = child; }
    return e;
}

static Expr* expr_binary(int op, Expr* l, Expr* r) {
    Expr* e = expr_alloc(EXPR_BINARY);
    if (e) { e->op = op; e->left = l; e->right = r; }
    return e;
}

static void expr_free(Expr* e) {
    if (!e) return;
    expr_free(e->left);
    expr_free(e->right);
    free(e);
}

/// Count operations in an expression tree.
static int expr_ops(Expr* e) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_CONST_INT: case EXPR_CONST_FLOAT: case EXPR_VAR: return 1;
        case EXPR_UNARY: return 1 + expr_ops(e->left);
        case EXPR_BINARY: return 1 + expr_ops(e->left) + expr_ops(e->right);
    }
    return 0;
}

/// Deep-copy an expression tree.
static Expr* expr_clone(Expr* e) {
    if (!e) return NULL;
    switch (e->kind) {
        case EXPR_CONST_INT: return expr_const_int(e->ival);
        case EXPR_CONST_FLOAT: return expr_const_float(e->fval);
        case EXPR_VAR: return expr_var(e->var_idx);
        case EXPR_UNARY: return expr_unary(e->op, expr_clone(e->left));
        case EXPR_BINARY: return expr_binary(e->op, expr_clone(e->left), expr_clone(e->right));
    }
    return NULL;
}

// ── Expression pretty-print ─────────────────────────────────────────────

static void expr_fprint(FILE* f, Expr* e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_CONST_INT:
            fprintf(f, "%" PRId64, e->ival);
            break;
        case EXPR_CONST_FLOAT:
            if (e->fval == (double)(int64_t)e->fval)
                fprintf(f, "%" PRId64 ".0f", (int64_t)e->fval);
            else
                fprintf(f, "%gf", e->fval);
            break;
        case EXPR_VAR:
            fprintf(f, "x%d", e->var_idx);
            break;
        case EXPR_UNARY: {
            const char* s = "";
            switch (e->op) {
                case TOK_MINUS: s = "-"; break;
                case TOK_BIT_NOT: s = "~"; break;
                default: s = "?"; break;
            }
            fprintf(f, "%s(", s);
            expr_fprint(f, e->left);
            fprintf(f, ")");
            break;
        }
        case EXPR_BINARY: {
            const char* s = "?";
            switch (e->op) {
                case TOK_PLUS: s = "+"; break;
                case TOK_MINUS: s = "-"; break;
                case TOK_STAR: s = "*"; break;
                case TOK_DIV: s = "/"; break;
                case TOK_BIT_AND: s = "&"; break;
                case TOK_BIT_OR: s = "|"; break;
                case TOK_BIT_XOR: s = "^"; break;
                case TOK_SHL: s = "<<"; break;
                case TOK_SHR: s = ">>"; break;
                default: s = "?"; break;
            }
            fprintf(f, "(");
            expr_fprint(f, e->left);
            fprintf(f, " %s ", s);
            expr_fprint(f, e->right);
            fprintf(f, ")");
            break;
        }
    }
}

// ── Expression evaluation ───────────────────────────────────────────────

/// Evaluate an expression on concrete inputs. Returns 0 on success,
/// -1 on evaluation error (div by zero, etc.).
/// For integer types, uses 64-bit signed wrapping arithmetic.
/// For float types, uses double.
static int expr_eval_int(Expr* e, const int64_t* params, size_t nparams,
                         int64_t* out) {
    if (!e || !out) return -1;
    switch (e->kind) {
        case EXPR_CONST_INT: *out = e->ival; return 0;
        case EXPR_CONST_FLOAT: *out = (int64_t)e->fval; return 0;
        case EXPR_VAR:
            if ((size_t)e->var_idx >= nparams) return -1;
            *out = params[e->var_idx];
            return 0;
        case EXPR_UNARY: {
            int64_t v;
            if (expr_eval_int(e->left, params, nparams, &v) != 0) return -1;
            switch (e->op) {
                case TOK_MINUS: *out = -v; return 0;
                case TOK_BIT_NOT: *out = ~v; return 0;
                default: return -1;
            }
        }
        case EXPR_BINARY: {
            int64_t l, r;
            if (expr_eval_int(e->left, params, nparams, &l) != 0) return -1;
            if (expr_eval_int(e->right, params, nparams, &r) != 0) return -1;
            switch (e->op) {
                case TOK_PLUS:    *out = l + r; return 0;
                case TOK_MINUS:   *out = l - r; return 0;
                case TOK_STAR:    *out = l * r; return 0;
                case TOK_DIV:     if (r == 0) return -1; *out = l / r; return 0;
                case TOK_BIT_AND: *out = l & r; return 0;
                case TOK_BIT_OR:  *out = l | r; return 0;
                case TOK_BIT_XOR: *out = l ^ r; return 0;
                case TOK_SHL:     if (r < 0) return -1; *out = l << r; return 0;
                case TOK_SHR:     if (r < 0) return -1; *out = l >> r; return 0;
                default: return -1;
            }
        }
    }
    return -1;
}

static int expr_eval_float(Expr* e, const double* params, size_t nparams,
                           double* out) {
    if (!e || !out) return -1;
    switch (e->kind) {
        case EXPR_CONST_INT: *out = (double)e->ival; return 0;
        case EXPR_CONST_FLOAT: *out = e->fval; return 0;
        case EXPR_VAR:
            if ((size_t)e->var_idx >= nparams) return -1;
            *out = params[e->var_idx];
            return 0;
        case EXPR_UNARY: {
            double v;
            if (expr_eval_float(e->left, params, nparams, &v) != 0) return -1;
            switch (e->op) {
                case TOK_MINUS: *out = -v; return 0;
                case TOK_BIT_NOT: *out = -(v + 1); return 0; // ~v = -v-1
                default: return -1;
            }
        }
        case EXPR_BINARY: {
            double l, r;
            if (expr_eval_float(e->left, params, nparams, &l) != 0) return -1;
            if (expr_eval_float(e->right, params, nparams, &r) != 0) return -1;
            switch (e->op) {
                case TOK_PLUS:    *out = l + r; return 0;
                case TOK_MINUS:   *out = l - r; return 0;
                case TOK_STAR:    *out = l * r; return 0;
                case TOK_DIV:     if (r == 0.0) return -1; *out = l / r; return 0;
                default: return -1;
            }
        }
    }
    return -1;
}

// ── Constant bank ───────────────────────────────────────────────────────

typedef struct {
    int64_t ivals[32];
    double  fvals[32];
    size_t  nint;
    size_t  nfloat;
} ConstBank;

static void const_bank_init(ConstBank* cb, int is_float) {
    cb->nint = 0;
    cb->nfloat = 0;
    if (is_float) {
        double f[] = {0.0, 1.0, 0.5, 2.0, 0.25, 0.3333, 0.75, 3.0, 4.0, 0.1, 0.01, -1.0, 10.0, 100.0, 255.0, 256.0};
        for (size_t i = 0; i < sizeof(f)/sizeof(f[0]) && cb->nfloat < 32; i++)
            cb->fvals[cb->nfloat++] = f[i];
    } else {
        int64_t v[] = {0, 1, -1, 2, 3, 4, 5, 8, 10, 16, 32, 64, 128, 255, 256, 65535, 0xFFFFFFFF, 0x80000000, 0x5F3759DF, 0x5F375A86};
        for (size_t i = 0; i < sizeof(v)/sizeof(v[0]) && cb->nint < 32; i++)
            cb->ivals[cb->nint++] = v[i];
    }
}

// ── Example data from AST ───────────────────────────────────────────────

typedef struct {
    int64_t* inputs;    // ninputs integers
    double*  finputs;   // ninputs floats
    int64_t  output;    // integer expected output
    double   foutput;   // float expected output
    double   tolerance; // 0 = hard, >0 = soft
    int      is_float;
    size_t   ninputs;
} Example;

/// Extract examples from a derivation AST node.
/// Returns number of examples, or -1 on error.
static int extract_examples(AstNode* deriv, Example** out_examples,
                            size_t* out_count, int is_float) {
    if (!deriv || deriv->kind != NODE_DERIVATION) return -1;
    size_t count = deriv->child_count;
    Example* ex = (Example*)calloc(count, sizeof(Example));
    if (!ex) return -1;
    size_t n = 0;

    for (size_t i = 0; i < count; i++) {
        AstNode* eg = deriv->children[i];
        if (eg->kind != NODE_DERIV_EXAMPLE || eg->child_count < 2) continue;

        // Find tolerance and output
        size_t last = eg->child_count;
        size_t output_idx = last - 1;
        double tol = 0.0;
        if (last > 0 && eg->children[last - 1]->kind == NODE_DERIV_TOLERANCE) {
            output_idx = last - 2;
            AstNode* tn = eg->children[last - 1];
            if (tn->child_count > 0 && tn->children[0]->kind == NODE_LITERAL_FLOAT)
                tol = tn->children[0]->token.value.f64;
        }

        size_t ninputs = output_idx;
        ex[n].tolerance = tol;
        ex[n].is_float = is_float;
        ex[n].ninputs = ninputs;

        if (is_float) {
            ex[n].finputs = (double*)calloc(ninputs + 1, sizeof(double));
            for (size_t j = 0; j < ninputs; j++) {
                AstNode* in = eg->children[j];
                if (in->kind == NODE_LITERAL_INT) ex[n].finputs[j] = (double)in->token.value.i64;
                else if (in->kind == NODE_LITERAL_FLOAT) ex[n].finputs[j] = in->token.value.f64;
            }
            AstNode* out_node = eg->children[output_idx];
            if (out_node->kind == NODE_LITERAL_INT) ex[n].foutput = (double)out_node->token.value.i64;
            else if (out_node->kind == NODE_LITERAL_FLOAT) ex[n].foutput = out_node->token.value.f64;
        } else {
            ex[n].inputs = (int64_t*)calloc(ninputs + 1, sizeof(int64_t));
            for (size_t j = 0; j < ninputs; j++) {
                AstNode* in = eg->children[j];
                if (in->kind == NODE_LITERAL_INT) ex[n].inputs[j] = in->token.value.i64;
            }
            AstNode* out_node = eg->children[output_idx];
            if (out_node->kind == NODE_LITERAL_INT) ex[n].output = out_node->token.value.i64;
        }
        n++;
    }

    *out_examples = ex;
    *out_count = n;
    return 0;
}

static void free_examples(Example* ex, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(ex[i].inputs);
        free(ex[i].finputs);
    }
    free(ex);
}

// ── Cost model ──────────────────────────────────────────────────────────

typedef struct {
    int     ops;        // operation count
    double  penalty;    // total normalized penalty (0 = all hard constraints satisfied)
    double  raw_error;  // raw error sum
    Expr*   expr;       // the candidate expression
} Candidate;

/// Evaluate a candidate on all examples. Returns 1 if hard constraints fail.
static int eval_candidate(Expr* e, const Example* examples, size_t nexamples,
                          int* out_ops, double* out_penalty) {
    int ops = expr_ops(e);
    double penalty = 0.0;

    for (size_t i = 0; i < nexamples; i++) {
        const Example* ex = &examples[i];
        if (ex->is_float) {
            double result;
            if (expr_eval_float(e, ex->finputs, ex->ninputs, &result) != 0) {
                *out_penalty = 1e10;
                return 1; // evaluation error
            }
            double error = fabs(result - ex->foutput);
            if (ex->tolerance == 0.0) {
                if (error > 1e-9) { *out_penalty = 1e10; return 1; } // hard fail
            } else {
                double raw = error > ex->tolerance ? error - ex->tolerance : 0.0;
                penalty += raw / (ex->tolerance > 1e-10 ? ex->tolerance : 1e-10);
            }
        } else {
            int64_t result;
            if (expr_eval_int(e, ex->inputs, ex->ninputs, &result) != 0) {
                *out_penalty = 1e10;
                return 1;
            }
            double error = (double)fabs((double)(result - ex->output));
            if (ex->tolerance == 0.0) {
                if (result != ex->output) { *out_penalty = 1e10; return 1; }
            } else {
                double raw = error > ex->tolerance ? error - ex->tolerance : 0.0;
                penalty += raw / (ex->tolerance > 1e-10 ? ex->tolerance : 1e-10);
            }
        }
    }

    *out_ops = ops;
    *out_penalty = penalty;
    return 0;
}

// ── Tree enumeration (simplified) ───────────────────────────────────────

/// Generate all 1-op leaf expressions.
static void gen_leaves(Expr** buf, size_t* count, size_t max, int nparams,
                       const ConstBank* cb, int is_float) {
    *count = 0;
    // Parameters
    for (int i = 0; i < nparams && *count < max; i++) {
        buf[(*count)++] = expr_var(i);
    }
    // Constants
    if (is_float) {
        for (size_t i = 0; i < cb->nfloat && *count < max; i++) {
            buf[(*count)++] = expr_const_float(cb->fvals[i]);
        }
    } else {
        for (size_t i = 0; i < cb->nint && *count < max; i++) {
            buf[(*count)++] = expr_const_int(cb->ivals[i]);
        }
    }
}

/// Number of ops = number of operators. This is a simplified generator
/// that builds expressions incrementally. For budget up to ~7, we use
/// a simple approach: start with leaves, then combine with operators.
typedef struct {
    Expr**  exprs;
    size_t  count;
    size_t  cap;
} ExprPool;

static void pool_init(ExprPool* p) {
    p->exprs = NULL; p->count = 0; p->cap = 0;
}

static void pool_add(ExprPool* p, Expr* e) {
    if (p->count >= p->cap) {
        size_t nc = p->cap == 0 ? 256 : p->cap * 2;
        Expr** ne = (Expr**)realloc(p->exprs, nc * sizeof(Expr*));
        if (!ne) return;
        p->exprs = ne;
        p->cap = nc;
    }
    p->exprs[p->count++] = e;
}

static void pool_free(ExprPool* p) {
    for (size_t i = 0; i < p->count; i++) expr_free(p->exprs[i]);
    free(p->exprs);
    p->exprs = NULL; p->count = 0; p->cap = 0;
}

/// Generate all expressions up to budget ops, with a cap on total count.
static int generate_all(ExprPool* all, int budget, int nparams,
                        const ConstBank* cb, int is_float, int* ops_used) {
    pool_init(all);

    // Leaves (1-op) — use a small constant set for sanity
    Expr* leaves[64];
    size_t nleaves = 0;
    gen_leaves(leaves, &nleaves, 64, nparams, cb, is_float);
    for (size_t i = 0; i < nleaves; i++) pool_add(all, leaves[i]);
    *ops_used = 1;

    // Cap to prevent explosion
    size_t MAX_EXPRS = 50000;

    for (int cur_ops = 2; cur_ops <= budget; cur_ops++) {
        size_t prev_count = all->count;
        if (prev_count > 2000) prev_count = 2000; // limit predecessors

        // Unary: -expr, ~expr
        int uops[] = {TOK_MINUS, TOK_BIT_NOT};
        for (int ui = 0; ui < 2; ui++) {
            for (size_t j = 0; j < prev_count && all->count < MAX_EXPRS; j++) {
                int child_ops = expr_ops(all->exprs[j]);
                if (child_ops == cur_ops - 1) {
                    pool_add(all, expr_unary(uops[ui], expr_clone(all->exprs[j])));
                }
            }
        }

        // Binary: left op right (commutative dedup: only try l <= r for +,*,&,|,^)
        int bops[] = {TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_DIV,
                      TOK_BIT_AND, TOK_BIT_OR, TOK_BIT_XOR,
                      TOK_SHL, TOK_SHR};
        for (int bi = 0; bi < 9 && all->count < MAX_EXPRS; bi++) {
            int commutative = (bops[bi] == TOK_PLUS || bops[bi] == TOK_STAR ||
                              bops[bi] == TOK_BIT_AND || bops[bi] == TOK_BIT_OR ||
                              bops[bi] == TOK_BIT_XOR);
            for (size_t l = 0; l < prev_count && all->count < MAX_EXPRS; l++) {
                size_t r_start = commutative ? l : 0;
                for (size_t r = r_start; r < prev_count && all->count < MAX_EXPRS; r++) {
                    int lops = expr_ops(all->exprs[l]);
                    int rops = expr_ops(all->exprs[r]);
                    if (lops + rops == cur_ops - 1) {
                        pool_add(all, expr_binary(bops[bi],
                                   expr_clone(all->exprs[l]),
                                   expr_clone(all->exprs[r])));
                    }
                }
            }
        }
        if (all->count >= MAX_EXPRS) break;
    }

    return 0;
}

// ── Pareto frontier ─────────────────────────────────────────────────────

typedef struct {
    Candidate* cands;
    size_t     count;
    size_t     cap;
} Frontier;

static void frontier_init(Frontier* f) {
    f->cands = NULL; f->count = 0; f->cap = 0;
}

static void frontier_add(Frontier* f, Candidate c) {
    if (f->count >= f->cap) {
        size_t nc = f->cap == 0 ? 16 : f->cap * 2;
        Candidate* ncands = (Candidate*)realloc(f->cands, nc * sizeof(Candidate));
        if (!ncands) return;
        f->cands = ncands;
        f->cap = nc;
    }
    f->cands[f->count++] = c;
}

static void frontier_free(Frontier* f) {
    // Don't free expr — they're owned by the pool
    free(f->cands);
    f->cands = NULL; f->count = 0; f->cap = 0;
}

/// Update the Pareto frontier with a new candidate.
static void frontier_update(Frontier* f, int ops, double penalty, Expr* e) {
    // Check if dominated by existing
    for (size_t i = 0; i < f->count; i++) {
        if (f->cands[i].ops <= ops && f->cands[i].penalty <= penalty) {
            // Existing dominates new candidate
            return;
        }
    }
    // Remove candidates dominated by new one
    size_t j = 0;
    for (size_t i = 0; i < f->count; i++) {
        if (f->cands[i].ops >= ops && f->cands[i].penalty >= penalty) {
            // New candidate dominates existing — drop it
            continue;
        }
        f->cands[j++] = f->cands[i];
    }
    f->count = j;

    Candidate c;
    c.ops = ops;
    c.penalty = penalty;
    c.raw_error = penalty * 0; // simplified
    c.expr = e;
    frontier_add(f, c);
}

/// Find the knee of the frontier (closest to origin).
static int frontier_knee(Frontier* f) {
    if (f->count == 0) return -1;
    int best = 0;
    double best_dist = 1e100;
    for (size_t i = 0; i < f->count; i++) {
        double dist = sqrt((double)f->cands[i].ops * f->cands[i].ops +
                           f->cands[i].penalty * f->cands[i].penalty);
        if (dist < best_dist) { best_dist = dist; best = (int)i; }
    }
    return best;
}

// ── Source mutation ──────────────────────────────────────────────────────

static int insert_body_into_source(const char* path, Expr* expr,
                                    const char* fname) {
    (void)fname;
    // Read the source file
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = (char*)malloc((size_t)len + 1);
    if (!src) { fclose(f); return -1; }
    fread(src, 1, (size_t)len, f);
    src[len] = '\0';
    fclose(f);

    // Find the function's opening brace position
    // Search for `fname` then find the first `{` after the `)`
    char* pos = strstr(src, fname);
    if (!pos) { free(src); return -1; }

    // Find the closing paren of the parameter list
    char* paren = strchr(pos, ')');
    if (!paren) { free(src); return -1; }

    // Find `:=` (derivation marker) or `{` (existing body)
    char* body_start = paren + 1;
    while (*body_start == ' ' || *body_start == '\t' || *body_start == '\n')
        body_start++;

    // Insert body BEFORE `:=` if present, otherwise at current position
    if (*body_start == ':') {
        // Insert body between `)` and `:=`
        // body_start points to `:`, which is the correct insertion point
    }
    // If there's already a body ({), don't modify

    // Generate the body string
    char body[4096];
    int n = snprintf(body, sizeof(body), "{\n    return ");
    // Use a FILE* to a temp string via sprintf-like approach
    // Write expr to a temp buffer manually
    {
        char tmp[2048] = {0};
        // Walk the expression tree and write to tmp
        // Simple approach: use expr_fprint on a temporary file
        FILE* tf = tmpfile();
        if (tf) {
            expr_fprint(tf, expr);
            rewind(tf);
            size_t r = fread(tmp, 1, sizeof(tmp) - 1, tf);
            tmp[r] = '\0';
            fclose(tf);
        }
        n += snprintf(body + n, sizeof(body) - (size_t)n, "%s", tmp);
    }
    snprintf(body + n, sizeof(body) - (size_t)n, ";\n}\n");

    // Calculate insertion point (after the function signature or derivation)
    long insert_offset = (long)(body_start - src);

    // Write modified source
    f = fopen(path, "wb");
    if (!f) { free(src); return -1; }
    fwrite(src, 1, (size_t)insert_offset, f);
    fwrite(body, 1, strlen(body), f);
    fwrite(src + insert_offset, 1, (size_t)(len - insert_offset), f);
    fclose(f);

    free(src);
    return 0;
}

// ── Main synthesis per function ─────────────────────────────────────────

static int synthesize_function(AstNode* func, const char* source_path,
                               ErrorList* errors, int interactive) {
    (void)errors;
    // Find derivation block
    AstNode* deriv = NULL;
    AstNode* body = NULL;
    int is_float = 0;

    for (size_t i = 0; i < func->child_count; i++) {
        AstNode* ch = func->children[i];
        if (ch->kind == NODE_DERIVATION) deriv = ch;
        if (ch->kind == NODE_BLOCK) body = ch;
    }

    // Only synthesize if there's a derivation block but NO body
    if (!deriv || body) return 0;

    // Determine if float by checking derivation examples
    for (size_t i = 0; i < deriv->child_count && !is_float; i++) {
        AstNode* eg = deriv->children[i];
        if (eg->kind == NODE_DERIV_EXAMPLE) {
            for (size_t j = 0; j < eg->child_count; j++) {
                if (eg->children[j]->kind == NODE_LITERAL_FLOAT ||
                    eg->children[j]->kind == NODE_DERIV_TOLERANCE) {
                    is_float = 1;
                    break;
                }
            }
        }
    }

    // Count parameters
    AstNode* param_list = NULL;
    for (size_t i = 0; i < func->child_count; i++) {
        if (func->children[i]->kind == NODE_PARAM_LIST) {
            param_list = func->children[i];
            break;
        }
    }
    int nparams = param_list ? (int)param_list->child_count : 0;

    // Extract examples
    Example* examples = NULL;
    size_t nexamples = 0;
    if (extract_examples(deriv, &examples, &nexamples, is_float) != 0 || nexamples == 0) {
        printf("  %.*s: no valid derivation examples\n",
               (int)func->token.len, func->token.text);
        return 1;
    }

    // Set up constant bank
    ConstBank cb;
    const_bank_init(&cb, is_float);

    // Generate all expressions up to budget
    int budget = 7; // default budget
    ExprPool all;
    int ops_used = 0;
    if (generate_all(&all, budget, nparams, &cb, is_float, &ops_used) != 0) {
        printf("  %.*s: expression generation failed\n",
               (int)func->token.len, func->token.text);
        free_examples(examples, nexamples);
        return 1;
    }

    // Evaluate all candidates and build Pareto frontier
    Frontier frontier;
    frontier_init(&frontier);

    for (size_t i = 0; i < all.count; i++) {
        int ops;
        double penalty;
        if (eval_candidate(all.exprs[i], examples, nexamples, &ops, &penalty) == 0) {
            // Candidate passes all hard constraints
            frontier_update(&frontier, ops, penalty, all.exprs[i]);
        }
    }

    // Report results
    char fname[256];
    int flen = (int)func->token.len;
    if (flen > 255) flen = 255;
    snprintf(fname, sizeof(fname), "%.*s", flen, func->token.text);

    if (frontier.count == 0) {
        printf("  %s: no valid expression found within budget=%d\n", fname, budget);
        frontier_free(&frontier);
        pool_free(&all);
        free_examples(examples, nexamples);
        return 1;
    }

    printf("\nPareto frontier for '%s' (budget=%d):\n", fname, budget);
    for (size_t i = 0; i < frontier.count; i++) {
        printf("  ops=%d  penalty=%.4f  ", frontier.cands[i].ops, frontier.cands[i].penalty);
        expr_fprint(stdout, frontier.cands[i].expr);
        printf("\n");
    }

    int knee = frontier_knee(&frontier);
    if (knee >= 0) {
        printf("  Knee at ops=%d\n", frontier.cands[knee].ops);
    }

    // Interactive selection or auto-pick knee
    int choice = knee;
    if (interactive && frontier.count > 1) {
        printf("Select expression (0-%zu, default=%d): ", frontier.count - 1, knee);
        char line[64];
        if (fgets(line, sizeof(line), stdin)) {
            int c;
            if (sscanf(line, "%d", &c) == 1 && c >= 0 && (size_t)c < frontier.count)
                choice = c;
        }
    }

    // Insert the selected expression as the function body
    if (choice >= 0 && (size_t)choice < frontier.count) {
        Expr* selected = frontier.cands[choice].expr;
        if (source_path && insert_body_into_source(source_path, selected, fname) == 0) {
            printf("  Synthesized body for '%s': ", fname);
            expr_fprint(stdout, selected);
            printf("\n");
        } else {
            printf("  Candidate for '%s': ", fname);
            expr_fprint(stdout, selected);
            printf("\n");
        }
    }

    frontier_free(&frontier);
    pool_free(&all);
    free_examples(examples, nexamples);
    return 0;
}

// ── Public API ───────────────────────────────────────────────────────────

int derive_synthesize(AstNode* root, const char* source_path,
                      ErrorList* errors, int interactive) {
    if (!root || root->kind != NODE_TRANSLATION_UNIT) return -1;
    (void)errors;

    int total = 0;
    int ok = 0;

    for (size_t i = 0; i < root->child_count; i++) {
        if (root->children[i]->kind == NODE_FUNCTION) {
            total++;
            int r = synthesize_function(root->children[i], source_path,
                                        errors, interactive);
            if (r == 0) ok++;
        }
    }

    if (total == 0) {
        printf("No functions with derivation blocks found.\n");
    } else {
        printf("\n%d/%d functions synthesized\n", ok, total);
    }

    return (ok == total) ? 0 : 1;
}
