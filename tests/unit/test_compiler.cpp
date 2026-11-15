#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/compiler.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/linker.hpp"
#include "kr16/vm.hpp"

namespace {

kr16::VmResult run_c_stats(const std::string& source,
                           const std::vector<std::string>& arguments = {},
                           bool optimize = true) {
    kr16::CompileOptions options;
    options.optimize = optimize;
    const auto compiled = kr16::compile_c(source, options);
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
    return result;
}

int run_c(const std::string& source,
          const std::vector<std::string>& arguments = {},
          bool optimize = true) {
    return run_c_stats(source, arguments, optimize).exit_code;
}

std::string compile_text(const std::string& source, bool optimize = true) {
    kr16::CompileOptions options;
    options.optimize = optimize;
    const auto compiled = kr16::compile_c(source, options);
    REQUIRE(compiled.ok);
    return compiled.assembly;
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
    std::ifstream fileio_file(std::string(KR16_SOURCE_DIR) + "/resources/fileio.c",
                             std::ios::binary);
    REQUIRE(fileio_file.good());
    const std::string fileio_source(
        (std::istreambuf_iterator<char>(fileio_file)),
        std::istreambuf_iterator<char>());
    kr16::CompileOptions fileio_options;
    fileio_options.source_name = "fileio.c";
    fileio_options.include_paths.push_back(std::string(KR16_SOURCE_DIR) + "/include");
    const auto fileio_compiled = kr16::compile_c(fileio_source, fileio_options);
    REQUIRE(fileio_compiled.ok);
    const auto fileio_assembled =
        kr16::assemble_text(fileio_compiled.assembly, kr16::AsmOptions{});
    REQUIRE(fileio_assembled.ok);
    const auto linked = kr16::link_objects(
        {libc.object, fileio_assembled.object, assembled.object});
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
    std::ifstream fileio_file(std::string(KR16_SOURCE_DIR) + "/resources/fileio.c",
                             std::ios::binary);
    REQUIRE(fileio_file.good());
    const std::string fileio_source(
        (std::istreambuf_iterator<char>(fileio_file)),
        std::istreambuf_iterator<char>());
    kr16::CompileOptions fileio_options;
    fileio_options.source_name = "fileio.c";
    fileio_options.include_paths.push_back(std::string(KR16_SOURCE_DIR) + "/include");
    const auto fileio_compiled = kr16::compile_c(fileio_source, fileio_options);
    REQUIRE(fileio_compiled.ok);
    const auto fileio_assembled =
        kr16::assemble_text(fileio_compiled.assembly, kr16::AsmOptions{});
    REQUIRE(fileio_assembled.ok);
    const auto linked = kr16::link_objects(
        {libc.object, fileio_assembled.object, assembled.object});
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

TEST_CASE("compiled casts work") {
    CHECK(run_c("int main(void) { return (int)3; }\n") == 3);
    CHECK(run_c("int main(void) { int x = 0x123; return (char)x; }\n") ==
          0x23);
    CHECK(run_c("int main(void) { int x = 7; int *p = &x; "
                "return *(int *)(void *)p; }\n") == 7);
    CHECK(run_c("int main(void) { return sizeof((char *)0); }\n") == 2);
    CHECK(run_c("int main(void) { int a[3]; a[1] = 9; "
                "return ((int *)a)[1]; }\n") == 9);
    CHECK(run_c_libc_exit(
              "#include <stdlib.h>\n"
              "int main(void) { int *p = (int *)malloc(4); p[0] = 42; "
              "int v = p[0]; free(p); return v; }\n") == 42);
}

TEST_CASE("constant folding and constant control flow") {
    CHECK(run_c("int main(void) { return 2 + 3 * 4; }\n") == 14);
    CHECK(run_c("int main(void) { return (1 << 4) | 3; }\n") == 19);
    CHECK(run_c("int main(void) { return (7 / 2) + (7 % 3); }\n") == 4);
    CHECK(run_c("int main(void) { int s = 1; if (0) { s = 9; } "
                "return s + 7; }\n") == 8);
    CHECK(run_c("int main(void) { int s = 1; if (1) { s = 9; } "
                "return s + 7; }\n") == 16);
    CHECK(run_c("int main(void) { int s = 4; while (0) { s = 9; } "
                "return s; }\n") == 4);
    CHECK(run_c("int main(void) { int i = 0; while (1) { i = i + 1; "
                "if (i == 3) break; } return i; }\n") == 3);
    CHECK(run_c("int main(void) { int s = 1; for (s = 2; 0;) { s = 9; } "
                "return s; }\n") == 2);
    CHECK(run_c("int main(void) { return 1 ? 7 : 9; }\n") == 7);
    CHECK(run_c("int main(void) { return 0 ? 7 : 9; }\n") == 9);
}

TEST_CASE("optimized codegen removes simple expression spills") {
    const auto optimized = compile_text(
        "int main(void) { int a = 2; int b = 3; return a + b; }\n");
    CHECK(optimized.find("PUSH R0") == std::string::npos);
    CHECK(optimized.find("POP R0") == std::string::npos);
    CHECK(optimized.find("ADD R") != std::string::npos);
    CHECK(optimized.find("ST [R4-") == std::string::npos);

    const auto plain = compile_text(
        "int main(void) { int a = 2; int b = 3; return a + b; }\n", false);
    CHECK(plain.find("PUSH R0") != std::string::npos);

    const auto assigned = compile_text(
        "int main(void) { int s = 1; s = s + 1; return s; }\n");
    CHECK(assigned.find("PUSH R0") == std::string::npos);
    CHECK(assigned.find("ST [R4-") == std::string::npos);
}

TEST_CASE("target-directed expressions preserve values") {
    CHECK(run_c("int main(void) { int a = 4; int b = 3; "
                "return (a + b) * 2 + (a - b); }\n") == 15);
    CHECK(run_c("int main(void) { int a = 4; int b = 3; "
                "return (a > b) + (a == 4); }\n") == 2);
    CHECK(run_c("int main(void) { int s = 1; s = s + 1; s += 3; "
                "return s; }\n") == 5);
    CHECK(run_c("int main(void) { int s = 1; s = s + 1; s += 3; "
                "return s; }\n", {}, false) == 5);
    CHECK(run_c("int main(void) { int i = 0; int s = 0; "
                "for (i = 0; i < 5; i++) { if (i == 2) continue; "
                "s += i; } return s; }\n") == 8);
    CHECK(run_c("int main(void) { int i = 0; int s = 0; "
                "do { s++; i++; } while (i < 3); return s; }\n") == 3);
    CHECK(run_c("int main(void) { int a = 0; if (1) a = 4; "
                "else a = 9; return a; }\n") == 4);
}

TEST_CASE("IR peepholes fuse branches and fold immediates") {
    const auto assembly = compile_text(
        "int main(void) { int i = 0; int s = 0; "
        "while (i < 10) { s = s + i; i++; } return s; }\n");
    CHECK(assembly.find("CMP R") != std::string::npos);
    CHECK(assembly.find(", #10") != std::string::npos);
    CHECK(assembly.find("BGE ") != std::string::npos);
    CHECK(assembly.find("ADD R") != std::string::npos);
    CHECK(assembly.find("Lircmp") == std::string::npos);
}

TEST_CASE("constant casts and sizeof conditions are folded") {
    CHECK(run_c("int main(void) { int s = 0; if ((int)1) s = 5; "
                "return s; }\n") == 5);
    CHECK(run_c("int main(void) { int s = 0; if (sizeof(char)) s = 5; "
                "return s; }\n") == 5);
    const auto assembly = compile_text(
        "int main(void) { int s = 0; if ((int)1) s = 5; return s; }\n");
    CHECK(assembly.find("CMP R0, #0") == std::string::npos);
}

TEST_CASE("IR dead stores are removed") {
    const auto assembly =
        compile_text("int main(void) { int s = 0; s = 5; return s; }\n");
    CHECK(assembly.find("MOV R0, #0") == std::string::npos);
    CHECK(assembly.find("MOV R0, #5") != std::string::npos);
}

TEST_CASE("pure fallback expressions allocate registers and spill safely") {
    CHECK(run_c(
              "int f(int a, int b, int c, int d, int e, int g, int h, "
              "int i) { int x[2]; return ((a+b)*(c+d)) + "
              "((e+g)*(h+i)); }\n"
              "int main(void) { return f(1,2,3,4,5,6,7,8); }\n") == 186);
    CHECK(run_c(
              "int g(int x, int y) { return x - y; }\n"
              "int main(void) { int a = 5; int b = 2; "
              "return g(a+b, a-b); }\n") == 4);
}

TEST_CASE("fallback constant arithmetic uses immediates") {
    const auto assembly = compile_text(
        "int f(int n) { int a[2]; return n - 1; }\n"
        "int main(void) { return f(5); }\n");
    CHECK(assembly.find("SUB R0, #1") != std::string::npos);
    CHECK(assembly.find("MOV R1, #1") == std::string::npos);
}

TEST_CASE("simple multi-argument calls use argument registers directly") {
    const auto assembly = compile_text(
        "int show(char *fmt, int value) { return value; }\n"
        "int main(void) { int result = 5; "
        "return show(\"%d\\n\", result); }\n");
    CHECK(assembly.find("PUSH R0") == std::string::npos);
    CHECK(assembly.find("POP R1") == std::string::npos);
    CHECK(assembly.find("MOV R1, #.Lstr") != std::string::npos);
    CHECK(assembly.find("MOV R2, R3") != std::string::npos);
    CHECK(assembly.find("CALL show") != std::string::npos);
}

TEST_CASE("fallback conditions branch directly on comparisons") {
    const auto assembly = compile_text(
        "int f(int x) { int a[2]; if (x == 0) return 1; return 2; }\n"
        "int main(void) { return f(0); }\n");
    CHECK(assembly.find("CMP R0, R1") == std::string::npos);
    CHECK(assembly.find("CMP R0, #0") != std::string::npos);
    CHECK(assembly.find("BNE ") != std::string::npos);
}

TEST_CASE("single-argument calls avoid stack shuffling") {
    const auto assembly = compile_text(
        "int f(int x) { return x; }\n"
        "int main(void) { return f(5); }\n");
    CHECK(assembly.find("PUSH R0") == std::string::npos);
    CHECK(assembly.find("POP R1") == std::string::npos);
    CHECK(assembly.find("MOV R1, #5") != std::string::npos);
}

TEST_CASE("IR dead code and strength reduction") {
    const auto assembly = compile_text(
        "int main(void) { int dead = 123; int x = 5; return x * 4; }\n");
    CHECK(assembly.find("#123") == std::string::npos);
    CHECK(assembly.find("SHL R") != std::string::npos);
    CHECK(assembly.find("MUL R") == std::string::npos);
}

TEST_CASE("IR allocator falls back safely") {
    CHECK(run_c("int main(void) { int a=1; int b=2; int c=3; int d=4; "
                "int e=5; int f=6; return a+b+c+d+e+f; }\n") == 21);
    CHECK(run_c("int add(int a, int b) { return a + b; }\n"
                "int main(void) { return add(2, 3); }\n") == 5);
}

TEST_CASE("compiled functions can exceed the short branch range") {
    std::string source = "int main(void) { int s = 0; if (1) {\n";
    for (int i = 0; i < 160; ++i) {
        source += "  s = s + 1;\n";
    }
    source += "} return s; }\n";
    CHECK(run_c(source) == 160);

    std::string skipped = "int main(void) { int s = 0; if (0) {\n";
    for (int i = 0; i < 160; ++i) {
        skipped += "  s = s + 1;\n";
    }
    skipped += "} return s + 7; }\n";
    CHECK(run_c(skipped) == 7);

    std::string looped = "int main(void) { int i = 0; int s = 0; "
                         "while (i < 3) {\n";
    for (int i = 0; i < 160; ++i) {
        looped += "  s = s + 1;\n";
    }
    looped += "  i = i + 1;\n} return s == 480 ? 0 : 1; }\n";
    CHECK(run_c(looped) == 0);
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
    CHECK(run_c_libc_exit(
              "#include <stdlib.h>\n"
              "int main(void) { char *p = malloc(4); p[0] = 65; "
              "p[1] = 66; int v = p[1]; free(p); return v; }\n") == 66);
    CHECK(run_c_libc_exit(
              "#include <stdlib.h>\n"
              "int main(void) { int *p = calloc(3, 2); p[2] = 7; "
              "int v = p[1] + p[2]; free(p); return v; }\n") == 7);
    CHECK(run_c_libc_exit(
              "#include <stdlib.h>\n"
              "int main(void) { char *p = malloc(2); p[0] = 'A'; "
              "p = realloc(p, 4); p[1] = 'B'; "
              "int v = p[0] + p[1]; free(p); return v; }\n") == 131);
    CHECK(run_c_libc_exit(
              "#include <stdlib.h>\n"
              "int main(void) { return abs(-5); }\n") == 5);
    CHECK(run_c_libc_exit(
              "#include <stdlib.h>\n"
              "int main(void) { return atoi(\"123\"); }\n") == 123);
    CHECK(run_c_libc_exit(
              "#include <string.h>\n"
              "int main(void) { char b[8]; strcpy(b, \"hi\"); "
              "strcat(b, \"Z\"); return b[2]; }\n") == 90);
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
        "struct P { int x; }; "
        "int main(void) { struct P p; int counter = 5; "
        "return counter; }\n",
        options);
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
    const auto* frames = linked.image.find_section(".debug_frames");
    REQUIRE(frames != nullptr);
    const std::string frame_data(frames->data.begin(), frames->data.end());
    CHECK(frame_data.find("main|") != std::string::npos);
    const auto* types = linked.image.find_section(".debug_types");
    REQUIRE(types != nullptr);
    const std::string type_data(types->data.begin(), types->data.end());
    CHECK(type_data.find("struct P|2") != std::string::npos);
}

TEST_CASE("debug source lines resume after included headers") {
    kr16::CompileOptions options;
    options.debug = true;
    options.source_name = "included.c";
    const auto result = kr16::compile_c(
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "  return 0;\n"
        "}\n",
        options);
    REQUIRE(result.ok);
    CHECK(result.assembly.find(".loc 0 3") != std::string::npos);
    CHECK(result.assembly.find(".loc 0 28") == std::string::npos);
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




TEST_CASE("constant array indexes fold into the frame address") {
    const auto assembly = compile_text(
        "int main(void) { int values[3]; values[2] = 7; return values[2]; }\n");
    CHECK(assembly.find("LEA R0, [R4-") != std::string::npos);
    CHECK(assembly.find("SHL R0, #1") == std::string::npos);
    CHECK(run_c("int main(void) { int values[3]; values[2] = 7; "
                "return values[2]; }\n") == 7);
}

TEST_CASE("constant struct member addresses fold into the frame") {
    const auto assembly = compile_text(
        "struct P { int x; int y; }; "
        "int main(void) { struct P p; p.y = 7; return p.y; }\n");
    CHECK(assembly.find("LEA R0, [R4-") != std::string::npos);
    CHECK(run_c("struct P { int x; int y; }; "
                "int main(void) { struct P p; p.y = 7; return p.y; }\n") == 7);
}

TEST_CASE("optimized loop benchmark reduces instruction count") {
    const std::string source =
        "int main(void) { int i; int j; int sum = 0; "
        "for (i = 0; i < 4; i++) { "
        "  for (j = 0; j < 4; j++) { sum += i + j; } "
        "} return sum; }\n";
    const auto optimized = compile_text(source, true);
    const auto plain = compile_text(source, false);
    const auto count_instructions = [](const std::string& text) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size();) {
            const auto end = text.find('\n', i);
            const auto line = text.substr(i, end - i);
            if (!line.empty() && line.front() == ' ') {
                ++count;
            }
            if (end == std::string::npos) {
                break;
            }
            i = end + 1;
        }
        return count;
    };
    CHECK(count_instructions(optimized) < count_instructions(plain));
    CHECK(run_c(source) == 48);
}


TEST_CASE("high-pressure scalar functions fall back without miscompiling") {
    const std::string source =
        "int sum8(int a, int b, int c, int d, int e, int f, int g, int h) { "
        "return a + b + c + d + e + f + g + h; }\n"
        "int main(void) { return sum8(1, 2, 3, 4, 5, 6, 7, 8); }\n";
    CHECK(run_c(source) == 36);
}


TEST_CASE("IR spills values when scalar pressure exceeds registers") {
    const std::string source =
        "int f(int x) { int a = x + 1; int b = a + 2; int c = b + 3; "
        "int d = c + 4; int e = d + 5; int g = e + 6; int h = g + 7; "
        "return a + b + c + d + e + g + h; }\n"
        "int main(void) { return f(1); }\n";
    CHECK(run_c(source) == 91);
}



TEST_CASE("spilled values feed branch comparisons") {
    const std::string source =
        "int f(int x) { int a = x + 1; int b = a + 2; int c = b + 3; "
        "int d = c + 4; int e = d + 5; int g = e + 6; int h = g + 7; "
        "if (a + b + c + d + e + g + h > 50) return 7; return 9; }\n"
        "int main(void) { return f(1); }\n";
    CHECK(run_c(source) == 7);
}

TEST_CASE("spilled char values preserve truncation") {
    const std::string source =
        "int f(char x) { char a = x + 1; char b = a + 2; char c = b + 3; "
        "char d = c + 4; char e = d + 5; char g = e + 6; char h = g + 7; "
        "return a + b + c + d + e + g + h; }\n"
        "int main(void) { return f(1); }\n";
    CHECK(run_c(source) == 91);
}

TEST_CASE("loops keep pressure values correct across spill stores") {
    const std::string source =
        "int f(int n) { int a = 1; int b = 2; int c = 3; int d = 4; "
        "int e = 5; int g = 6; int i; int s = 0; "
        "for (i = 0; i < n; i++) { s = s + a + b + c + d + e + g; } "
        "return s; }\n"
        "int main(void) { return f(3); }\n";
    CHECK(run_c(source) == 63);
}

TEST_CASE("basic-block copy propagation removes alias chains") {
    const std::string source =
        "int f(int x) { int a = x; int b = a; int c = b; return c; }\n"
        "int main(void) { return f(3); }\n";
    const auto assembly = compile_text(source, true);
    const auto start = assembly.find(".global f");
    const auto end = assembly.find(".global main");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    const auto body = assembly.substr(start, end - start);
    CHECK(body.find("MOV R0, R1") != std::string::npos);
    CHECK(body.find("MOV R1, R0") == std::string::npos);
    CHECK(run_c(source) == 3);
}

TEST_CASE("IR reuses constants within a basic block") {
    const std::string source =
        "int f(int a) { int x = 2; int y = 2; return a + x + y; }\n"
        "int main(void) { return f(1); }\n";
    const auto assembly = compile_text(source, true);
    const auto start = assembly.find(".global f");
    const auto end = assembly.find(".global main");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    const auto body = assembly.substr(start, end - start);
    CHECK(body.find("MOV R0, #2") != std::string::npos);
    CHECK(body.find("MOV R1, #2") == std::string::npos);
    CHECK(run_c(source) == 5);
}

TEST_CASE("counted loops strength-reduce constant induction multiplication") {
    const std::string source =
        "int f(int n) { int i; int s = 0; "
        "for (i = 0; i < n; i++) { s = s + i * 3; } return s; }\n"
        "int main(void) { return f(4); }\n";
    const auto assembly = compile_text(source, true);
    const auto start = assembly.find(".global f");
    const auto end = assembly.find(".global main");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    CHECK(assembly.substr(start, end - start).find("MUL") == std::string::npos);
    CHECK(run_c(source) == 18);
}

TEST_CASE("pointer conditions branch directly without boolean materialization") {
    const std::string source =
        "int same(char *a, char *b) { "
        "while (*a != 0 && *b != 0) { "
        "if (*a != *b) return 0; a = a + 1; b = b + 1; } "
        "return *a == *b; }\n"
        "int main(void) { char a[4]; char b[4]; "
        "a[0] = 65; a[1] = 66; a[2] = 0; "
        "b[0] = 65; b[1] = 66; b[2] = 0; "
        "return same(a, b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-") != std::string::npos);
    CHECK(assembly.find("LD R2, [R4-") != std::string::npos);
    CHECK(assembly.find("LD R0, [R4-2]") == std::string::npos);
    CHECK(assembly.find("CMP R0, #0") != std::string::npos);
    CHECK(assembly.find("CMP R0, R1") != std::string::npos);
    CHECK(run_c(source) == 1);
}

TEST_CASE("arithmetic and indexed loop conditions branch directly") {
    const std::string source =
        "int f(char *s, int max) { int i = 0; "
        "while (i < max - 1 && s[i] != 0) { i = i + 1; } return i; }\n"
        "int main(void) { char s[4]; s[0] = 1; s[1] = 1; s[2] = 0; "
        "return f(s, 4); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("purecmp") == std::string::npos);
    CHECK(run_c(source) == 2);
}

TEST_CASE("simple indexed stores avoid push-pop address shuffling") {
    const std::string source =
        "int f(char *d) { d[2] = 0; return d[2]; }\n"
        "int main(void) { char b[4]; b[0] = 1; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    const auto start = assembly.find(".global f");
    const auto end = assembly.find(".global main");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    CHECK(assembly.substr(start, end - start).find("PUSH R0") ==
          std::string::npos);
    CHECK(run_c(source) == 0);
}

TEST_CASE("indexed base loads directly into R1") {
    const std::string source =
        "int f(char *d, int i) { d[i] = 0; return d[i]; }\n"
        "int main(void) { char b[4]; b[1] = 7; return f(b, 1); }\n";
    const auto assembly = compile_text(source, true);
    const auto start = assembly.find(".global f");
    const auto end = assembly.find(".global main");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    const auto body = assembly.substr(start, end - start);
    CHECK(body.find("MOV R1, R3") != std::string::npos);
    CHECK(body.find("MOV R1, R0") == std::string::npos);
    CHECK(run_c(source) == 0);
}

TEST_CASE("multi-argument calls keep the first three arguments in registers") {
    const std::string source =
        "int add4(int a, int b, int c, int d) { return a + b + c + d; }\n"
        "int f(int x) { return add4(x, 1, 2, 3); }\n"
        "int main(void) { return f(4); }\n";
    const auto assembly = compile_text(source, true);
    const auto start = assembly.find(".global f");
    const auto end = assembly.find(".global main");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    const auto body = assembly.substr(start, end - start);
    CHECK(body.find("CALL add4") != std::string::npos);
    const auto first_push = body.find("PUSH R0");
    REQUIRE(first_push != std::string::npos);
    CHECK(body.find("PUSH R0", first_push + 1) == std::string::npos);
    CHECK(run_c(source) == 10);
}



TEST_CASE("for-loop induction variable is cached in R3") {
    const std::string source =
        "int f(char *p, int n) { int i; int s = 0; "
        "for (i = 0; i < n; i++) { s = s + i; } return s; }\n"
        "int main(void) { char b[1]; return f(b, 4); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-") != std::string::npos);
    CHECK(assembly.find("ADD R3, #1") != std::string::npos);
    CHECK(run_c(source) == 6);
}


TEST_CASE("boolean returns branch directly to return values") {
    const std::string source =
        "int f(char *p, int a, int b) { return a == b; }\n"
        "int main(void) { char b[1]; return f(b, 4, 4); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("fastcmp") == std::string::npos);
    CHECK(assembly.find("purecmp") == std::string::npos);
    CHECK(assembly.find("rettrue") != std::string::npos);
    CHECK(run_c(source) == 1);
}

TEST_CASE("single-update while pointer loop caches R3") {
    const std::string source =
        "int f(char *p) { while (*p != 0) { p = p + 1; } return *p; }\n"
        "int main(void) { char b[2]; b[0] = 1; b[1] = 0; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-") != std::string::npos);
    CHECK(assembly.find("MOV R3, R0") != std::string::npos);
    CHECK(run_c(source) == 0);
}



TEST_CASE("read-only parameters are cached in R3") {
    const std::string source =
        "int f(char *p, int x) { return x + 1; }\n"
        "int main(void) { char b[1]; return f(b, 4); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("MOV R3, R2") != std::string::npos);
    CHECK(assembly.find("LD R0, [R4-4]") == std::string::npos);
    CHECK(assembly.find("ST [R4-4], R2") == std::string::npos);
    CHECK(assembly.find("SUB SP, #2") != std::string::npos);
    CHECK(run_c(source) == 5);
}





TEST_CASE("call-free complex stores stage the address in R2") {
    const std::string source =
        "int f(char *p, int n) { p[n] = n + 1; return p[n]; }\n"
        "int main(void) { char b[2]; return f(b, 1); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("MOV R2, R0") != std::string::npos);
    CHECK(assembly.find("PUSH R0") == std::string::npos);
    CHECK(run_c(source) == 2);
}



TEST_CASE("read-only locals are cached in R3") {
    const std::string source =
        "int f(char *p) { int x = 5; return x; }\n"
        "int main(void) { char b[1]; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(assembly.find("LD R0, [R4-4]") == std::string::npos);
    CHECK(run_c(source) == 5);
}


TEST_CASE("updated locals stay cached across if") {
    const std::string source =
        "int f(char *p) { p = p + 0; int x = 1; if (p) { x = 2; } "
        "return x; }\n"
        "int main(void) { char b[1]; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(assembly.find("MOV R3, R0") != std::string::npos);
    CHECK(run_c(source) == 2);
}

TEST_CASE("updated locals stay cached across switch") {
    const std::string source =
        "int f(char *p) { p = p + 0; int x = 1; "
        "switch (p[0]) { case 1: x = 3; break; default: x = 4; } "
        "return x; }\n"
        "int main(void) { char b[1]; b[0] = 1; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(assembly.find("MOV R3, R0") != std::string::npos);
    CHECK(run_c(source) == 3);
}

TEST_CASE("cached parameters spill and reload around calls") {
    const std::string source =
        "int inc(int n) { return n + 1; }\n"
        "int f(char *p, int x) { int y = inc(x); return x + y; }\n"
        "int main(void) { char b[1]; return f(b, 4); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("ST [R4-4], R3") != std::string::npos);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(run_c(source) == 9);
}

TEST_CASE("cached locals spill and reload around calls") {
    const std::string source =
        "int inc(int n) { return n + 1; }\n"
        "int f(char *p) { int x = 1; int y = inc(x); x = x + 1; "
        "return x + y; }\n"
        "int main(void) { char b[1]; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("ST [R4-4], R3") != std::string::npos);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(run_c(source) == 4);
}




TEST_CASE("two live locals use R3 and R2") {
    const std::string source =
        "int f(char *p) { int x = 1; int y = 2; x = x + 1; "
        "y = y + x; return x + y; }\n"
        "int main(void) { char b[1]; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(assembly.find("LD R2, [R4-6]") != std::string::npos);
    CHECK(run_c(source) == 6);
}

TEST_CASE("benchmark gate for pointer, call-spill, and two-local caches") {
    const auto count_instructions = [](const std::string& text) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size();) {
            const auto end = text.find('\n', i);
            const auto line = text.substr(i, end - i);
            if (!line.empty() && line.front() == ' ') {
                ++count;
            }
            if (end == std::string::npos) {
                break;
            }
            i = end + 1;
        }
        return count;
    };
    struct Case {
        std::string source;
        int expected;
    };
    const std::vector<Case> cases = {
        {"int same(char *a, char *b) { while (*a != 0 && *b != 0) { "
         "if (*a != *b) return 0; a = a + 1; b = b + 1; } "
         "return *a == *b; }\n"
         "int main(void) { char a[4]; char b[4]; a[0]=65; a[1]=66; "
         "a[2]=0; b[0]=65; b[1]=66; b[2]=0; return same(a,b); }\n",
         1},
        {"int inc(int n) { return n + 1; }\n"
         "int f(char *p, int x) { int y = inc(x); return x + y; }\n"
         "int main(void) { char b[1]; return f(b, 4); }\n",
         9},
        {"int f(char *p) { int x = 1; int y = 2; x = x + 1; "
         "y = y + x; return x + y; }\n"
         "int main(void) { char b[1]; return f(b); }\n",
         6},
    };
    for (const auto& test : cases) {
        const auto optimized = compile_text(test.source, true);
        const auto plain = compile_text(test.source, false);
        CHECK(count_instructions(optimized) <
              count_instructions(plain));
        CHECK(run_c(test.source) == test.expected);
    }
}

TEST_CASE("cached loop pointers spill around zero-argument calls") {
    const std::string source =
        "int tick(void) { return 1; }\n"
        "int f(char *p) { int n = 0; while (*p) { p = p + 1; "
        "n = n + tick(); } return n; }\n"
        "int main(void) { char b[3]; b[0] = 1; b[1] = 1; b[2] = 0; "
        "return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("ST [R4-2], R3") != std::string::npos);
    CHECK(assembly.find("LD R3, [R4-2]") != std::string::npos);
    CHECK(run_c(source) == 2);
}

TEST_CASE("benchmark gate covers call-heavy cache shapes") {
    const auto count_instructions = [](const std::string& text) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size();) {
            const auto end = text.find('\n', i);
            const auto line = text.substr(i, end - i);
            if (!line.empty() && line.front() == ' ') {
                ++count;
            }
            if (end == std::string::npos) {
                break;
            }
            i = end + 1;
        }
        return count;
    };
    struct Case {
        std::string source;
        int expected;
    };
    const std::vector<Case> cases = {
        {"int tick(void) { return 1; }\n"
         "int f(char *p) { int n = 0; while (*p) { p = p + 1; "
         "n = n + tick(); } return n; }\n"
         "int main(void) { char b[3]; b[0]=1; b[1]=1; b[2]=0; "
         "return f(b); }\n",
         2},
        {"int inc(int n) { return n + 1; }\n"
         "int f(char *p) { int x = 1; int y = inc(x); "
         "x = x + 1; return x + y; }\n"
         "int main(void) { char b[1]; return f(b); }\n",
         4},
    };
    for (const auto& test : cases) {
        const auto optimized = compile_text(test.source, true);
        const auto plain = compile_text(test.source, false);
        CHECK(count_instructions(optimized) <
              count_instructions(plain));
        CHECK(run_c(test.source) == test.expected);
    }
}

TEST_CASE("cached loop pointers spill around one-argument calls") {
    const std::string source =
        "int tick(int n) { return n; }\n"
        "int f(char *p) { int n = 0; while (*p) { p = p + 1; "
        "n = tick(n) + 1; } return n; }\n"
        "int main(void) { char b[3]; b[0]=1; b[1]=1; b[2]=0; "
        "return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("ST [R4-2], R3") != std::string::npos);
    CHECK(assembly.find("LD R3, [R4-2]") != std::string::npos);
    CHECK(run_c(source) == 2);
}


TEST_CASE("nested calls fall back safely for parameter caches") {
    const std::string source =
        "int inc(int n) { return n + 1; }\n"
        "int f(char *p, int x) { int y; y = inc(inc(x)); "
        "return x + y; }\n"
        "int main(void) { char b[1]; return f(b, 4); }\n";
    CHECK(run_c(source) == 10);
}


TEST_CASE("three-argument scalar functions use the fallback backend") {
    CHECK(run_c("int add3(int a, int b, int c) { return a + b + c; }\n"
                "int main(void) { return add3(1, 2, 3); }\n") == 6);
    const std::string source =
        "int add3(int a, int b, int c) { return a + b + c; }\n"
        "int f(char *p) { int x = 1; int y = add3(x, 2, 3); "
        "return x + y; }\n"
        "int main(void) { char b[1]; return f(b); }\n";
    CHECK(run_c(source) == 7);
}

TEST_CASE("live-span ranking drives two-local allocation") {
    const std::string source =
        "int f(char *p) { int a = 1; int b = 2; a = a + 1; "
        "b = b + a; int c = 3; b = b + c; return a + b + c; }\n"
        "int main(void) { char b[1]; return f(b); }\n";
    const auto assembly = compile_text(source, true);
    CHECK(assembly.find("LD R3, [R4-4]") != std::string::npos);
    CHECK(assembly.find("LD R2, [R4-6]") != std::string::npos);
    CHECK(run_c(source) == 12);
}

TEST_CASE("C-for-U compilation stays within instruction budget") {
    std::ifstream input(std::string(KR16_SOURCE_DIR) +
                        "/tests/kevin/C-for-U.c");
    REQUIRE(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto assembly = compile_text(buffer.str(), true);
    const auto count = [](const std::string& text) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < text.size();) {
            const auto end = text.find('\n', i);
            const auto line = text.substr(i, end - i);
            if (!line.empty() && line.front() == ' ') {
                ++count;
            }
            if (end == std::string::npos) {
                break;
            }
            i = end + 1;
        }
        return count;
    };
    CHECK(count(assembly) < 5600);
    CHECK(assembly.size() < 95000);
}



TEST_CASE("call-heavy cache shapes use fewer cycles at O1") {
    const std::string source =
        "int tick(void) { return 1; }\n"
        "int f(char *p) { int n = 0; while (*p) { p = p + 1; "
        "n = n + tick(); } return n; }\n"
        "int main(void) { char b[3]; b[0]=1; b[1]=1; b[2]=0; "
        "return f(b); }\n";
    const auto plain = run_c_stats(source, {}, false);
    const auto optimized = run_c_stats(source, {}, true);
    CHECK(plain.exit_code == 2);
    CHECK(optimized.exit_code == 2);
    CHECK(optimized.cycles < plain.cycles);
    CHECK(optimized.instructions < plain.instructions);
}
