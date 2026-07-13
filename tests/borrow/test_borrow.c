// 2026-07-13 — Borrow checker unit tests.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "lexer.h"
#include "parser.h"
#include "typecheck.h"
#include "borrow.h"
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

static int borrow_check_str(const char* c2src, ErrorList* err) {
    Parser parser = parser_create(c2src, strlen(c2src), "<test>", err);
    AstNode* ast = parser_parse(&parser);
    if (err->has_errors) return -1;
    SymbolTable* sym = NULL;
    typecheck_ast(ast, err, &sym);
    if (err->has_errors) { symtab_destroy(sym); ast_free_tree(ast); return -1; }
    int r = borrow_check(ast, sym, err);
    symtab_destroy(sym);
    ast_free_tree(ast);
    return r;
}

// ── Tests ────────────────────────────────────────────────────────────────

static void test_borrow_basic(void) {
    TEST("basic read — no error");
    ErrorList* err = errlist_create();
    int r = borrow_check_str("[1][1]\nint32_t f(int32_t x) { return x; }\n", err);
    assert(r == 0);
    errlist_destroy(err);
    PASS();

    TEST("basic write — no error");
    ErrorList* err2 = errlist_create();
    int r2 = borrow_check_str("[1][1]\nint32_t f() { int32_t x = 42; return x; }\n", err2);
    assert(r2 == 0);
    errlist_destroy(err2);
    PASS();
}

static void test_borrow_free(void) {
    TEST("free on owned — no error");
    ErrorList* err = errlist_create();
    int r = borrow_check_str("[1][1]\nvoid f() { int32_t* p; p = 0; free(p); }\n", err);
    assert(r == 0);
    errlist_destroy(err);
    PASS();
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(void) {
    printf("\nC² Borrow Checker Unit Tests\n");
    printf("============================\n\n");

    test_borrow_basic();
    test_borrow_free();

    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
