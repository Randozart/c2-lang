// 2026-07-13 — Unit tests for the C² code generator.
//   Tests AST-to-C output for various constructs.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "codegen.h"
#include "error.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-50s ", name); \
    tests_run++; \
} while (0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while (0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while (0)

static const char* transpile(const char* src, ErrorList* err) {
    Parser parser = parser_create(src, strlen(src), "test", err);
    AstNode* root = parser_parse(&parser);
    if (!root) return NULL;

    static Codegen cg;
    codegen_init(&cg, err);
    const char* output = codegen_generate(&cg, root);

    ast_free_tree(root);
    return output;
}

static void test_emit_empty(void) {
    TEST("emit empty translation unit");
    ErrorList* err = errlist_create();
    const char* out = transpile("", err);
    assert(out != NULL);
    int found_open = 0;
    for (const char* p = out; *p; p++) {
        if (*p == '{' || *p == '}') { found_open = 1; break; }
    }
    // Should have no braces in empty output
    assert(!found_open);
    errlist_destroy(err);
    PASS();
}

static void test_emit_function(void) {
    TEST("emit function with contract");
    ErrorList* err = errlist_create();
    const char* src = "[x > 0][result == x + 1]\n"
                      "int32_t increment(int32_t x) {\n"
                      "    return x + 1;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "int32_t") != NULL);
    assert(strstr(out, "increment") != NULL);
    assert(strstr(out, "return") != NULL);
    assert(strstr(out, "x + 1") != NULL);
    // Contracts should be stripped from output
    assert(strstr(out, "[") == NULL || strstr(out, "// [") != NULL || strstr(out, "/*") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_function_call(void) {
    TEST("emit function call");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t double_it(int32_t x) {\n"
                      "    return add(x, x);\n"
                      "}\n"
                      "[1][1]\n"
                      "int32_t add(int32_t a, int32_t b) {\n"
                      "    return a + b;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "add(x, x)") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_when(void) {
    TEST("emit when guard as if");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t abs(int32_t x) {\n"
                      "    when x < 0 -> return -x;\n"
                      "    return x;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    // when desugars to if in emitted C
    assert(strstr(out, "if") != NULL);
    errlist_destroy(err);
    PASS();

    TEST("emit when block");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\n"
                       "int32_t test(int32_t x) {\n"
                       "    when x > 0 {\n"
                       "        return 1;\n"
                       "    }\n"
                       "    return 0;\n"
                       "}\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    assert(strstr(out2, "if") != NULL);
    errlist_destroy(err2);
    PASS();
}

static void test_emit_while(void) {
    TEST("emit while loop");
    ErrorList* err = errlist_create();
    const char* src = "[n >= 0][result == n * (n + 1) / 2]\n"
                      "int32_t sum_n(int32_t n) {\n"
                      "    int32_t i = 0;\n"
                      "    int32_t s = 0;\n"
                      "    while (i <= n) {\n"
                      "        s = s + i;\n"
                      "        i = i + 1;\n"
                      "    }\n"
                      "    return s;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "while") != NULL);
    assert(strstr(out, "i <= n") != NULL);
    assert(strstr(out, "i = i + 1") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_for(void) {
    TEST("emit for loop");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t sum_to_n(int32_t n) {\n"
                      "    int32_t s = 0;\n"
                      "    for (int32_t i = 0; i < n; i = i + 1) {\n"
                      "        s = s + i;\n"
                      "    }\n"
                      "    return s;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "for") != NULL);
    assert(strstr(out, "i = 0") != NULL);
    assert(strstr(out, "i < n") != NULL);
    assert(strstr(out, "i = i + 1") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_pointer(void) {
    TEST("emit pointer parameter and deref");
    ErrorList* err = errlist_create();
    const char* src = "[ptr != 0][1]\n"
                      "void write(int32_t* ptr, int32_t val) {\n"
                      "    *ptr = val;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "*ptr") != NULL || strstr(out, "* ptr") != NULL);
    assert(strstr(out, "*ptr") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_derivation(void) {
    TEST("emit derivation block (assertion form)");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t add(int32_t a, int32_t b) := {\n"
                      "    1, 2 -> 3;\n"
                      "   10, 20 -> 30;\n"
                      "};\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_preprocessor(void) {
    TEST("emit #include directive");
    ErrorList* err = errlist_create();
    const char* src = "#include \"c2.h\"\n"
                      "[1][1]\n"
                      "int32_t foo(void) {\n"
                      "    return 0;\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "#include") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_string(void) {
    TEST("emit string literal");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "void greet(void) {\n"
                      "    printf(\"hello\");\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "\"hello\"") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_struct(void) {
    TEST("emit struct declaration");
    ErrorList* err = errlist_create();
    const char* src = "struct Point {\n"
                      "    int32_t x;\n"
                      "    int32_t y;\n"
                      "};\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "struct") != NULL);
    assert(strstr(out, "Point") != NULL);
    assert(strstr(out, "int32_t") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_enum(void) {
    TEST("emit enum declaration");
    ErrorList* err = errlist_create();
    const char* src = "enum Color { RED, GREEN, BLUE };\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "enum") != NULL);
    assert(strstr(out, "RED") != NULL);
    assert(strstr(out, "BLUE") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_typedef(void) {
    TEST("emit typedef");
    ErrorList* err = errlist_create();
    const char* src = "typedef int32_t myint;\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "typedef") != NULL);
    assert(strstr(out, "int32_t") != NULL);
    assert(strstr(out, "myint") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_emit_global_var(void) {
    TEST("emit global variable");
    ErrorList* err = errlist_create();
    const char* src = "int32_t counter = 0;\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "int32_t counter = 0") != NULL);
    errlist_destroy(err);
    PASS();

    TEST("emit global array");
    ErrorList* err2 = errlist_create();
    const char* src2 = "int32_t buffer[256];\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    assert(strstr(out2, "int32_t buffer[256]") != NULL);
    errlist_destroy(err2);
    PASS();
}

int main(void) {
    printf("\nC² Codegen Unit Tests\n");
    printf("=====================\n\n");

    test_emit_empty();
    test_emit_function();
    test_emit_function_call();
    test_emit_when();
    test_emit_while();
    test_emit_for();
    test_emit_pointer();
    test_emit_derivation();
    test_emit_preprocessor();
    test_emit_string();
    test_emit_struct();
    test_emit_enum();
    test_emit_typedef();
    test_emit_global_var();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
