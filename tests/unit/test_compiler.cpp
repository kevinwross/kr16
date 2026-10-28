#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/compiler.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/linker.hpp"
#include "kr16/vm.hpp"

namespace {

int run_c(const std::string& source,
          const std::vector<std::string>& arguments = {}) {
    const auto compiled = kr16::compile_c(source, kr16::CompileOptions{});
    REQUIRE(compiled.ok);
    const auto assembled =
        kr16::assemble_text(compiled.assembly, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto linked = kr16::link_objects({assembled.object});
    REQUIRE(linked.ok);
    static int counter = 0;
    const auto path = std::filesystem::current_path() /
                      ("kr16_c_test_" + std::to_string(counter++) + ".kex");
    {
        std::ofstream output(path, std::ios::binary);
        const auto bytes = linked.image.write();
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto result = kr16::run_vm(path.string(), [&]() {
        kr16::VmOptions options;
        options.program_args = arguments;
        return options;
    }());
    std::filesystem::remove(path);
    REQUIRE(result.ok);
    return result.exit_code;
}

std::string run_c_with_libc(const std::string& source,
                            const std::vector<std::string>& arguments = {}) {    const auto compiled = kr16::compile_c(source, kr16::CompileOptions{});
    REQUIRE(compiled.ok);
    const auto assembled =
        kr16::assemble_text(compiled.assembly, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto libc = kr16::assemble_file(
        std::string(KR16_SOURCE_DIR) + "/resources/libc.s", kr16::AsmOptions{});
    REQUIRE(libc.ok);
    const auto linked = kr16::link_objects({libc.object, assembled.object});
    REQUIRE(linked.ok);
    static int counter = 0;
    const auto path = std::filesystem::current_path() /
                      ("kr16_c_libc_" + std::to_string(counter++) + ".kex");
    {
        std::ofstream output(path, std::ios::binary);
        const auto bytes = linked.image.write();
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    kr16::VmOptions options;
    options.program_args = arguments;
    const auto result = kr16::run_vm(path.string(), options);
    std::filesystem::remove(path);
    REQUIRE(result.ok);
    return result.output;
}

int run_c_libc_exit(const std::string& source) {
    const auto compiled = kr16::compile_c(source, kr16::CompileOptions{});
    REQUIRE(compiled.ok);
    const auto assembled =
        kr16::assemble_text(compiled.assembly, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto libc = kr16::assemble_file(
        std::string(KR16_SOURCE_DIR) + "/resources/libc.s", kr16::AsmOptions{});
    REQUIRE(libc.ok);
    const auto linked = kr16::link_objects({libc.object, assembled.object});
    REQUIRE(linked.ok);
    static int counter = 0;
    const auto path = std::filesystem::current_path() /
                      ("kr16_c_libc_exit_" + std::to_string(counter++) + ".kex");
    {
        std::ofstream output(path, std::ios::binary);
        const auto bytes = linked.image.write();
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto result = kr16::run_vm(path.string(), kr16::VmOptions{});
    std::filesystem::remove(path);
    REQUIRE(result.ok);
    return result.exit_code;
}

}  // namespace

TEST_CASE("compile a minimal C program") {
    const auto result = kr16::compile_c(
        "int main(void) { return 42; }\n", kr16::CompileOptions{});
    const std::string compile_message =
        result.diagnostics.empty()
            ? "no diagnostic"
            : (std::to_string(result.diagnostics[0].line) + ":" +
               std::to_string(result.diagnostics[0].column) + ": " +
               result.diagnostics[0].message);
    INFO(compile_message);
    REQUIRE(result.ok);
    CHECK(result.assembly.find("main:") != std::string::npos);
    CHECK(result.assembly.find("MOV R0, #42") != std::string::npos);
    CHECK(result.assembly.find("_start:") != std::string::npos);
    const auto assembled =
        kr16::assemble_text(result.assembly, kr16::AsmOptions{});
    if (!assembled.ok) {
        INFO(result.assembly);
        const std::string message =
            assembled.diagnostics.empty() ? "no diagnostic"
                                          : assembled.diagnostics[0].message;
        INFO(message);
    }
    CHECK(assembled.ok);
}

TEST_CASE("compile loops and arithmetic") {
    const std::string source =
        "int main(void) {\n"
        "  int sum = 0;\n"
        "  int i;\n"
        "  for (i = 0; i < 5; i++) sum = sum + i;\n"
        "  return sum;\n"
        "}\n";
    const auto result = kr16::compile_c(source, kr16::CompileOptions{});
    REQUIRE(result.ok);
    const auto assembled =
        kr16::assemble_text(result.assembly, kr16::AsmOptions{});
    if (!assembled.ok) {
        INFO(result.assembly);
        const std::string message =
            assembled.diagnostics.empty() ? "no diagnostic"
                                          : assembled.diagnostics[0].message;
        INFO(message);
    }
    CHECK(assembled.ok);
}

TEST_CASE("compiler includes local headers") {
    const auto dir = std::filesystem::current_path();
    const auto header = dir / "kr16_header_test.h";
    {
        std::ofstream output(header);
        output << "#define HEADER_VALUE 7\n";
    }
    kr16::CompileOptions options;
    options.source_name = (dir / "kr16_header_main.c").string();
    const auto compiled = kr16::compile_c(
        "#include \"kr16_header_test.h\"\n"
        "int main(void) { return HEADER_VALUE; }\n",
        options);
    std::filesystem::remove(header);
    REQUIRE(compiled.ok);
    CHECK(compiled.assembly.find("MOV R0, #7") != std::string::npos);

    const auto incdir = dir / "kr16_incdir_test";
    std::filesystem::create_directories(incdir);
    {
        std::ofstream output(incdir / "custom.h");
        output << "#define CUSTOM_VALUE 9\n";
    }
    kr16::CompileOptions include_options;
    include_options.source_name = (dir / "kr16_incdir_main.c").string();
    include_options.include_paths.push_back(incdir.string());
    const auto with_path = kr16::compile_c(
        "#include <custom.h>\n"
        "int main(void) { return CUSTOM_VALUE; }\n",
        include_options);
    std::filesystem::remove_all(incdir);
    REQUIRE(with_path.ok);
    CHECK(with_path.assembly.find("MOV R0, #9") != std::string::npos);
}

TEST_CASE("compiled C runs end-to-end") {
    CHECK(run_c("int main(void) { return 42; }\n") == 42);
    CHECK(run_c("int main(void) { int i; int s = 0; "
                "for (i = 0; i < 5; i++) s = s + i; return s; }\n") == 10);
    CHECK(run_c("int add(int a, int b) { return a + b; } "
                "int main(void) { return add(3, 4); }\n") == 7);
}

TEST_CASE("compiled arrays and pointers work") {
    CHECK(run_c("int main(void) { int a[3]; a[0] = 4; a[1] = 5; "
                "return a[0] + a[1]; }\n") == 9);
    CHECK(run_c("int main(void) { int x = 7; int *p = &x; return *p; }\n") ==
          7);
}

TEST_CASE("compiled control flow works") {
    CHECK(run_c("int main(void) { int x = 0; if (1) x = 3; else x = 9; "
                "return x; }\n") == 3);
    CHECK(run_c("int main(void) { int i = 0; while (i < 6) i++; return i; }\n") ==
          6);
    CHECK(run_c("int main(void) { int i = 0; do { i++; } while (i < 6); "
                "return i; }\n") == 6);
    CHECK(run_c("int main(void) { int i = 0; int s = 0; do { i++; "
                "if (i == 3) continue; s = s + i; } while (i < 5); "
                "return s; }\n") == 12);
    CHECK(run_c("int main(void) { int x = 2; switch (x) { case 1: return 1; "
                "case 2: return 2; default: return 9; } return 0; }\n") == 2);
    CHECK(run_c("int main(void) { int x = 1; int s = 0; switch (x) { "
                "case 1: s = s + 1; case 2: s = s + 2; break; "
                "default: s = 9; } return s; }\n") == 3);
    CHECK(run_c("int main(void) { int x = 5; switch (x) { case 1: return 1; "
                "default: break; } return 7; }\n") == 7);
    CHECK(run_c("enum Color { RED, GREEN = 5, BLUE }; "
                "int main(void) { return BLUE; }\n") == 6);
    CHECK(run_c("enum { A = 2, B }; "
                "int main(void) { return A + B; }\n") == 5);
    CHECK(run_c("enum Color { RED, GREEN }; "
                "int main(void) { enum Color c; c = GREEN; return c; }\n") ==
          1);
    CHECK(run_c("typedef int Word; "
                "int main(void) { Word x = 5; return x; }\n") == 5);
    CHECK(run_c("typedef enum { A = 3, B } E; "
                "int main(void) { E x = B; return x; }\n") == 4);
    CHECK(run_c("typedef char *Str; "
                "int main(void) { Str p = \"A\"; return p[0]; }\n") == 65);
    CHECK(run_c("struct Point { int x; int y; }; "
                "int main(void) { struct Point p; p.x = 3; p.y = 4; "
                "return p.x + p.y; }\n") == 7);
    CHECK(run_c("struct Point { int x; int y; }; "
                "int main(void) { struct Point p; struct Point *q = &p; "
                "q->x = 5; q->y = 6; return q->x + q->y; }\n") == 11);
    CHECK(run_c("struct Pair { char a; int b; }; "
                "int main(void) { struct Pair p; p.a = 'A'; p.b = 2; "
                "return p.a + p.b; }\n") == 67);
    CHECK(run_c("struct Pair { char a; int b; }; "
                "int main(void) { return sizeof(struct Pair); }\n") == 4);
    CHECK(run_c("struct Pair { int x; int y; }; "
                "int main(void) { struct Pair a; struct Pair b; "
                "a.x = 1; a.y = 2; b = a; return b.x + b.y; }\n") == 3);
    CHECK(run_c("struct Pair { int x; int y; }; "
                "int main(void) { struct Pair p = {3, 4}; "
                "return p.x + p.y; }\n") == 7);
    CHECK(run_c("int main(void) { int a[3] = {1, 2, 3}; "
                "return a[1] + a[2]; }\n") == 5);
    CHECK(run_c("int main(void) { char s[3] = {'A', 'B', 0}; "
                "return s[1]; }\n") == 66);
    CHECK(run_c("int a[3] = {1, 2, 3}; "
                "int main(void) { return a[0] + a[2]; }\n") == 4);
    CHECK(run_c("struct Pair { char a; int b; }; "
                "struct Pair p = {'A', 5}; "
                "int main(void) { return p.a + p.b; }\n") == 70);
    CHECK(run_c("union U { int i; char c; }; "
                "int main(void) { union U u; u.i = 0x4142; "
                "return u.c; }\n") == 66);
    CHECK(run_c("union U { int i; char c; }; "
                "int main(void) { return sizeof(union U); }\n") == 2);
    CHECK(run_c("union U { int i; char c[2]; }; "
                "int main(void) { union U u; u.i = 0x4142; "
                "return u.c[1]; }\n") == 65);
    CHECK(run_c("struct S { int a[2]; }; "
                "int main(void) { struct S s; s.a[0] = 3; s.a[1] = 4; "
                "return s.a[0] + s.a[1]; }\n") == 7);
    CHECK(run_c("int main(void) { int x = 5; x += 3; x *= 2; "
                "x -= 1; return x; }\n") == 15);
    CHECK(run_c("int main(void) { int a[2] = {3, 4}; a[1] += 5; "
                "return a[1]; }\n") == 9);
    CHECK(run_c("struct P { int x; }; "
                "int main(void) { struct P p; p.x = 5; p.x += 7; "
                "return p.x; }\n") == 12);
    CHECK(run_c("int main(void) { int i = 0; "
                "loop: i++; if (i < 3) goto loop; return i; }\n") == 3);
    CHECK(run_c("struct Node { int v; struct Node *next; }; "
                "int main(void) { struct Node a; struct Node b; "
                "a.v = 1; a.next = &b; b.v = 2; b.next = 0; "
                "return a.next->v; }\n") == 2);
    CHECK(run_c("int a[] = {1, 2, 3}; "
                "int main(void) { return a[2]; }\n") == 3);
    CHECK(run_c("int main(void) { int a[] = {4, 5}; "
                "return sizeof(a) + a[1]; }\n") == 9);
    CHECK(run_c("#define N 5\n"
                "int main(void) { return N; }\n") == 5);
    CHECK(run_c("#define A 2\n"
                "#define B A + 3\n"
                "int main(void) { return B; }\n") == 5);
    CHECK(run_c("#ifdef MISSING\n"
                "int main(void) { return 1; }\n"
                "#else\n"
                "int main(void) { return 2; }\n"
                "#endif\n") == 2);
    CHECK(run_c("#if 0\n"
                "int main(void) { return 1; }\n"
                "#else\n"
                "int main(void) { return 3; }\n"
                "#endif\n") == 3);
    CHECK(run_c("#define ADD(a, b) ((a) + (b))\n"
                "int main(void) { return ADD(2, 3); }\n") == 5);
    CHECK(run_c("#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
                "int main(void) { return MAX(3, 7); }\n") == 7);
    CHECK(run_c("#if 0\n"
                "int main(void) { return 1; }\n"
                "#elif 1\n"
                "int main(void) { return 2; }\n"
                "#else\n"
                "int main(void) { return 3; }\n"
                "#endif\n") == 2);
    CHECK(run_c("#if 1\n"
                "int main(void) { return 4; }\n"
                "#elif 1\n"
                "int main(void) { return 5; }\n"
                "#endif\n") == 4);
    CHECK(run_c("#include <stdint.h>\n"
                "int main(void) { uint8_t x = 65; return x; }\n") == 65);
    CHECK(run_c("#include <stdbool.h>\n"
                "int main(void) { bool x = true; return x; }\n") == 1);
    CHECK(run_c_with_libc("#include <stdio.h>\n"
                          "int main(void) { putchar('A'); return 0; }\n") ==
          "A");
    CHECK(run_c_with_libc("#define STR(x) #x\n"
                          "int main(void) { puts(STR(hello)); return 0; }\n") ==
          "hello\n");
    CHECK(run_c("#define CAT(a, b) a##b\n"
                "int main(void) { int foobar = 7; return CAT(foo, bar); }\n") ==
          7);
    CHECK(run_c("int next(void) { static int n = 0; return ++n; } "
                "int main(void) { next(); next(); return next(); }\n") == 3);
    CHECK(run_c("int sum(void) { static int a[] = {1, 2, 3}; "
                "return a[0] + a[2]; } "
                "int main(void) { return sum(); }\n") == 4);
    CHECK(run_c("struct P { int x; int y; }; "
                "int sum(void) { static struct P p = {4, 5}; "
                "return p.x + p.y; } "
                "int main(void) { return sum(); }\n") == 9);
    CHECK(run_c("#include <stdarg.h>\n"
                "int sum(int n, ...) { va_list ap; va_start(ap, n); "
                "int s = 0; for (int i = 0; i < n; i++) "
                "s += va_arg(ap, int); va_end(ap); return s; }\n"
                "int main(void) { return sum(3, 4, 5, 6); }\n") == 15);
    CHECK(run_c("int main(void) { enum { A = 3, B }; return B; }\n") == 4);
    CHECK(run_c("struct S { int a[2]; int b; }; "
                "int main(void) { struct S s = {{1, 2}, 3}; "
                "return s.a[0] + s.a[1] + s.b; }\n") == 6);
    CHECK(run_c("struct S { int a[2]; int b; }; "
                "struct S s = {{1, 2}, 3}; "
                "int main(void) { return s.a[1] + s.b; }\n") == 5);
    CHECK(run_c("struct S { int a[2]; int b; }; "
                "int f(void) { static struct S s = {{1, 2}, 3}; "
                "return s.a[0] + s.b; } "
                "int main(void) { return f(); }\n") == 4);
    CHECK(run_c("int main(void) { volatile int x = 3; "
                "register int y = 4; return x + y; }\n") == 7);
    CHECK(run_c("int main(void) { _Bool b = 1; return b; }\n") == 1);
    CHECK(run_c("inline int f(void) { return 5; } "
                "int main(void) { return f(); }\n") == 5);
    CHECK(run_c("struct P { int x; int y; }; "
                "int sum(struct P p) { p.x = 9; return p.x + p.y; } "
                "int main(void) { struct P q = {2, 3}; "
                "return sum(q) + q.x; }\n") == 14);
    CHECK(run_c("struct P { int x; int y; }; "
                "struct P make(void) { struct P p = {3, 4}; return p; } "
                "int main(void) { struct P q = make(); "
                "return q.x + q.y; }\n") == 7);
}

TEST_CASE("compiled C calls libc") {
    CHECK(run_c_with_libc(
              "int main(void) { putchar('H'); putchar('i'); "
              "putchar(10); return 0; }\n") == "Hi\n");
    CHECK(run_c_with_libc(
              "int main(void) { puts(\"Hello\"); return 0; }\n") ==
          "Hello\n");
    CHECK(run_c_with_libc(
              "int main(void) { printf(\"%d %s %c\\n\", 42, \"hi\", '!'); "
              "return 0; }\n") == "42 hi !\n");
    CHECK(run_c_with_libc(
              "int putchar(int c); "
              "int main(void) { putchar('A'); return 0; }\n") == "A");
    CHECK(run_c_with_libc(
              "int printf(char *fmt, ...); "
              "int main(void) { printf(\"%d\\n\", 7); return 0; }\n") ==
          "7\n");
    CHECK(run_c_libc_exit(
              "#include <string.h>\n"
              "int main(void) { char b[8]; strcpy(b, \"hi\"); "
              "return strlen(b); }\n") == 2);
    CHECK(run_c_libc_exit(
              "#include <string.h>\n"
              "int main(void) { return strcmp(\"a\", \"a\") == 0; }\n") == 1);
    CHECK(run_c_libc_exit(
              "#include <string.h>\n"
              "int main(void) { char b[8]; memset(b, 'x', 3); "
              "return b[2]; }\n") == 120);
    CHECK(run_c_libc_exit(
              "#include <string.h>\n"
              "int main(void) { char b[8]; memcpy(b, \"abc\", 3); "
              "return b[1]; }\n") == 98);
}

TEST_CASE("compiled char locals and globals") {
    CHECK(run_c("char g; int main(void) { g = 'A'; return g; }\n") == 65);
    CHECK(run_c("int main(void) { char c = 'Z'; return c; }\n") == 90);
}

TEST_CASE("compiled main receives argc and argv") {
    CHECK(run_c("int main(int argc, char **argv) { return argc; }\n",
                {"a", "b"}) == 3);
    CHECK(run_c("int main(int argc, char **argv) "
                "{ return argv[1][0]; }\n",
                {"A"}) == 65);
}

TEST_CASE("compiler emits local variable debug records") {
    kr16::CompileOptions options;
    options.debug = true;
    options.source_name = "debug.c";
    const auto compiled = kr16::compile_c(
        "int main(void) { int counter = 5; return counter; }\n", options);
    REQUIRE(compiled.ok);
    CHECK(compiled.assembly.find(".debug_vars") != std::string::npos);
    CHECK(compiled.assembly.find("main|counter|") != std::string::npos);
    const auto assembled =
        kr16::assemble_text(compiled.assembly, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto linked = kr16::link_objects({assembled.object});
    REQUIRE(linked.ok);
    const auto* vars = linked.image.find_section(".debug_vars");
    REQUIRE(vars != nullptr);
    const std::string data(vars->data.begin(), vars->data.end());
    CHECK(data.find("main|counter|") != std::string::npos);
}

TEST_CASE("compiler emits source line information with -g") {
    kr16::CompileOptions options;
    options.debug = true;
    const auto result = kr16::compile_c(
        "int main(void) {\n  int x = 1;\n  return x;\n}\n", options);
    REQUIRE(result.ok);
    CHECK(result.assembly.find(".file 0") != std::string::npos);
    CHECK(result.assembly.find(".loc 0 2") != std::string::npos);
    const auto assembled =
        kr16::assemble_text(result.assembly, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto* line = assembled.object.find_section(".debug_line");
    REQUIRE(line != nullptr);
    const auto linked = kr16::link_objects({assembled.object});
    REQUIRE(linked.ok);
    const auto* linked_line = linked.image.find_section(".debug_line");
    REQUIRE(linked_line != nullptr);
    CHECK(!kr16::parse_debug_line(linked_line->data).empty());
}
