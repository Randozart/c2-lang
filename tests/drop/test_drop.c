// 2026-07-13 — Drop injection unit tests.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "lexer.h"
#include "parser.h"
#include "typecheck.h"
#include "drop.h"
#include "error.h"

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  %-55s ", name); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

// ── Helpers ──────────────────────────────────────────────────────────────

static int drop_inject_str(const char* c2src, ErrorList* err) {
    Parser parser = parser_create(c2src, strlen(c2src), "<test>", err);
    AstNode* ast = parser_parse(&parser);
    if (err->has_errors) return -1;
    SymbolTable* sym = NULL;
    typecheck_ast(ast, err, &sym);
    if (err->has_errors) { symtab_destroy(sym); ast_free_tree(ast); return -1; }
    int r = drop_inject(ast, sym, err);
    symtab_destroy(sym);
    ast_free_tree(ast);
    return r;
}

// ── Tests ────────────────────────────────────────────────────────────────

static void test_drop_basic(void) {
    TEST("drop injection runs without error");
    ErrorList* err = errlist_create();
    int r = drop_inject_str("[1][1]\nint32_t f(int32_t x) { return x; }\n", err);
    assert(r == 0);
    errlist_destroy(err);
    PASS();
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(void) {
    printf("\nC² Drop Injection Unit Tests\n");
    printf("============================\n\n");

    test_drop_basic();

    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
