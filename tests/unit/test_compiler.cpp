#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/compiler.hpp"
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
                            const std::vector<std::string>& arguments = {}) {
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

TEST_CASE("compiled __host_call uses the host block") {
    CHECK(run_c("int main(void) { int b[8]; b[0] = 0; "
                "return __host_call(0, b); }\n") == 0);
}
