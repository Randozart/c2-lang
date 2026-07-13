// 2026-07-13 — Full pipeline integration test.
//   Compiles C² source files, runs the resulting binaries,
//   and verifies correct output. Tests the complete
//   lexer→parser→typecheck→VRP→borrow→codegen→compile→run flow.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_total = 0;
static int test_index = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  %-55s ", name); \
    test_index++; \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)

// ── Helpers ──────────────────────────────────────────────────────────────

/// Write a string to a file. Returns 0 on success.
static int write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

/// Compile a C² source file with --emit-c to produce .c output.
/// Returns the path to the emitted C file (in _c2_out/).
static const char* emit_c(const char* c2_path) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "./build/c2c build '%s' --emit-c 2>/dev/null",
             c2_path);
    int r = system(cmd);
    if (r != 0) return NULL;
    // Determine the output C path
    static char c_path[4096];
    const char* base = strrchr(c2_path, '/');
    base = base ? base + 1 : c2_path;
    snprintf(c_path, sizeof(c_path), "_c2_out/%.*s.c",
             (int)(strstr(base, ".c2") ? (int)(strstr(base, ".c2") - base) : (int)strlen(base)),
             base);
    return c_path;
}

/// Compile C source with gcc and return the exit code.
static int compile_c(const char* c_path, const char* out_bin) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "gcc -std=c23 -Wall -Wextra -O2 -Iinclude '%s' -o '%s' 2>/dev/null",
             c_path, out_bin);
    return system(cmd);
}

// ── Test source snippets ────────────────────────────────────────────────

static const char* src_basic_contract =
    "[x > 0][x < 100]\n"
    "int32_t f(int32_t x) { return x; }\n"
    "[[result == 0]\n"
    "int32_t main(void) { return f(42) - 42; }\n";

static const char* src_borrow_own =
    "[1][1]\n"
    "void inc(own int32_t* p) { *p = *p + 1; }\n"
    "[[result == 3]\n"
    "int32_t main(void) {\n"
    "  int32_t x = 2;\n"
    "  inc(&x);\n"
    "  return x;\n"
    "}\n";

static const char* src_when_guard =
    "[1][1]\n"
    "int32_t abs_value(int32_t x) {\n"
    "  when x < 0 -> return -x;\n"
    "  return x;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return abs_value(-5) - 5; }\n";

static const char* src_while_loop =
    "[1][1]\n"
    "int32_t sum_to(int32_t n) {\n"
    "  int32_t s = 0;\n"
    "  int32_t i = 0;\n"
    "  while (i < n) { s = s + i; i = i + 1; }\n"
    "  return s;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return sum_to(5) - 10; }\n";  // 0+1+2+3+4 = 10

static const char* src_for_loop =
    "[1][1]\n"
    "int32_t sum_for(int32_t n) {\n"
    "  int32_t s = 0;\n"
    "  for (int32_t i = 0; i < n; i++) { s = s + i; }\n"
    "  return s;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return sum_for(5) - 10; }\n";

static const char* src_do_while =
    "[1][1]\n"
    "int32_t count_to(int32_t n) {\n"
    "  int32_t i = 0;\n"
    "  do { i = i + 1; } while (i < n);\n"
    "  return i;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return count_to(5) - 5; }\n";

// Derivation with body — uses derivation examples as assertions
// (body already present, no synthesis needed)
static const char* src_derivation =
    "int32_t add(int32_t a, int32_t b) {\n"
    "    return a + b;\n"
    "} := {\n"
    "    1, 2 -> 3;\n"
    "    10, 20 -> 30;\n"
    "};\n"
    "[[result == 0]\n"
    "int32_t main(void) { return add(3, 4) - 7; }\n";

static const char* src_ternary =
    "[1][1]\n"
    "int32_t max(int32_t a, int32_t b) {\n"
    "  return a > b ? a : b;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return max(7, 3) - 7; }\n";

static const char* src_multiple_funcs =
    "[x != 0][result == num / x]\n"
    "int32_t divide(int32_t num, int32_t x) { return num / x; }\n"
    "[[result >= -1]\n"
    "int32_t sign(int32_t x) {\n"
    "  when x > 0 -> return 1;\n"
    "  when x < 0 -> return -1;\n"
    "  return 0;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return divide(10, 2) - 5; }\n";

static const char* src_sequential_when =
    "[1][1]\n"
    "int32_t classify(int32_t x) {\n"
    "  when x > 0 -> return 1;\n"
    "  when x < 0 -> return -1;\n"
    "  return 0;\n"
    "}\n"
    "[[result == 0]\n"
    "int32_t main(void) { return classify(5) - 1 + classify(-3) + 1; }\n";

// ── Individual tests ─────────────────────────────────────────────────────

static void test_compile_and_run(const char* name, const char* source,
                                  int expected_exit) {
    TEST(name);
    mkdir("_c2_out", 0755);

    char c2_path[256], bin_path[256], out_path[256];
    snprintf(c2_path, sizeof(c2_path), "_c2_out/inttest_%d.c2", test_index);
    snprintf(out_path, sizeof(out_path), "_c2_out/inttest_%d", test_index);
    snprintf(bin_path, sizeof(bin_path), "_c2_out/inttest_bin_%d", test_index);

    if (write_file(c2_path, source) != 0) { FAIL("write"); return; }

    // Step 1: Compile C² → C
    const char* c_path = emit_c(c2_path);
    if (!c_path) { FAIL("c2c emit"); return; }

    // Step 2: Compile C → binary with gcc
    if (compile_c(c_path, bin_path) != 0) { FAIL("gcc compile"); return; }

    // Step 3: Run binary
    int exit_code = 0;
    {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "%s 2>/dev/null; echo EXIT:$?", bin_path);
        FILE* f = popen(cmd, "r");
        if (!f) { FAIL("run"); return; }
        char buf[4096];
        char* last_line = NULL;
        while (fgets(buf, sizeof(buf), f)) {
            if (strncmp(buf, "EXIT:", 5) == 0) {
                exit_code = atoi(buf + 5);
            }
            last_line = buf;
        }
        pclose(f);
        (void)last_line;
    }

    if (exit_code == expected_exit) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "exit %d (expected %d)", exit_code, expected_exit);
        FAIL(msg);
    }

    // Cleanup test files
    unlink(c2_path);
    unlink(bin_path);
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(void) {
    printf("\nC² Full Pipeline Integration Tests\n");
    printf("==================================\n\n");

    // Make sure build directory exists
    mkdir("_c2_out", 0755);

    test_compile_and_run("basic contract", src_basic_contract, 0);
    test_compile_and_run("borrow/own params", src_borrow_own, 3);
    test_compile_and_run("when guard + abs", src_when_guard, 0);
    test_compile_and_run("while loop", src_while_loop, 0);
    test_compile_and_run("for loop", src_for_loop, 0);
    test_compile_and_run("do-while loop", src_do_while, 0);
    test_compile_and_run("derivation block", src_derivation, 0);
    test_compile_and_run("ternary expression", src_ternary, 0);
    test_compile_and_run("multiple functions", src_multiple_funcs, 0);
    test_compile_and_run("sequential when guards", src_sequential_when, 0);
    // Pointer arithmetic with arrays — array indexing (NODE_INDEX) not fully supported yet

    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
