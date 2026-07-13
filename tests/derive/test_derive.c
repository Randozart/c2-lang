// 2026-07-13 — Derivation synthesis engine unit tests.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "lexer.h"
#include "parser.h"
#include "typecheck.h"
#include "derive.h"
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

static int derive_str(const char* c2src, ErrorList* err) {
    Parser parser = parser_create(c2src, strlen(c2src), "<test>", err);
    AstNode* ast = parser_parse(&parser);
    if (err->has_errors) return -1;
    typecheck_ast(ast, err, NULL);
    if (err->has_errors) return -1;
    int r = derive_synthesize(ast, NULL, err, 0);
    ast_free_tree(ast);
    return r;
}

// ── Tests ────────────────────────────────────────────────────────────────

static void test_derive_identity(void) {
    TEST("derive identity from examples");
    ErrorList* err = errlist_create();
    int r = derive_str("int32_t id(int32_t x) := { 0 -> 0; 1 -> 1; 5 -> 5; };\n", err);
    assert(r == 0); // should find x
    errlist_destroy(err);
    PASS();
}

static void test_derive_add(void) {
    TEST("derive addition from examples");
    ErrorList* err = errlist_create();
    int r = derive_str("int32_t add(int32_t a, int32_t b) := { 0, 0 -> 0; 1, 2 -> 3; 3, 4 -> 7; };\n", err);
    assert(r == 0); // should find a + b
    errlist_destroy(err);
    PASS();
}

static void test_derive_single_param(void) {
    TEST("derive single-param expression");
    ErrorList* err = errlist_create();
    int r = derive_str("int32_t f(int32_t x) := { 0 -> 1; 1 -> 2; 2 -> 3; };\n", err);
    assert(r == 0); // should find x + 1
    errlist_destroy(err);
    PASS();
}

static void test_derive_no_body(void) {
    TEST("no body to synthesize — has body already");
    ErrorList* err = errlist_create();
    int r = derive_str("int32_t f(int32_t x) { return x; } := { 0 -> 0; 1 -> 1; };\n", err);
    assert(r == 0); // nothing to do
    errlist_destroy(err);
    PASS();
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(void) {
    printf("\nC² Derive Synthesis Unit Tests\n");
    printf("==============================\n\n");

    test_derive_identity();
    test_derive_add();
    test_derive_single_param();
    test_derive_no_body();

    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
