add_executable(kr16_tests
  test_main.cpp
  unit/test_version.cpp
  unit/test_cli.cpp
  unit/test_container.cpp
  unit/test_debug_objects.cpp
  unit/test_container_fixtures.cpp
  unit/test_instruction.cpp
  unit/test_cpu.cpp
  unit/test_assembler.cpp
  unit/test_disassembler.cpp
  unit/test_machine.cpp
  unit/test_vm.cpp
  unit/test_linker.cpp
  unit/test_debug_info.cpp
  unit/test_compiler.cpp
  unit/test_host.cpp
  unit/test_dap.cpp
)
target_include_directories(kr16_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party"
)
target_compile_definitions(kr16_tests PRIVATE
  KR16_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_executable(kr16_golden_gen EXCLUDE_FROM_ALL golden/generate.cpp)
target_link_libraries(kr16_golden_gen PRIVATE kr16_core)
target_link_libraries(kr16_tests PRIVATE kr16_core)
if(MSVC)
  # doctest intentionally specializes std::tuple for its stringification.
  target_compile_options(kr16_tests PRIVATE /wd5285)
endif()

add_test(NAME kr16.unit COMMAND kr16_tests)
set_tests_properties(kr16.unit PROPERTIES LABELS "unit;smoke")

add_test(NAME kr16.help COMMAND kr16 help)
set_tests_properties(kr16.help PROPERTIES LABELS "unit;cli")

add_test(NAME kr16.unknown COMMAND kr16 nope)
set_tests_properties(kr16.unknown PROPERTIES LABELS "unit;cli" WILL_FAIL TRUE)

add_test(NAME kr16.version COMMAND kr16 version)
set_tests_properties(kr16.version PROPERTIES LABELS "unit;smoke")

add_test(NAME kr16.as.raw
  COMMAND kr16 as "${CMAKE_CURRENT_SOURCE_DIR}/integration/asm_min.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/asm_min.bin" --raw --base 0x100
)
set_tests_properties(kr16.as.raw PROPERTIES LABELS "integration")

add_test(NAME kr16.dis.raw
  COMMAND kr16 dis "${CMAKE_CURRENT_BINARY_DIR}/asm_min.bin"
    --raw --base 0x100
)
set_tests_properties(kr16.dis.raw PROPERTIES LABELS "integration"
  DEPENDS kr16.as.raw)

add_test(NAME kr16.as.object
  COMMAND kr16 as "${CMAKE_CURRENT_SOURCE_DIR}/integration/asm_min.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/asm_min.o"
)
set_tests_properties(kr16.as.object PROPERTIES LABELS "integration")

add_test(NAME kr16.dis.object
  COMMAND kr16 dis "${CMAKE_CURRENT_BINARY_DIR}/asm_min.o"
)
set_tests_properties(kr16.dis.object PROPERTIES LABELS "integration"
  DEPENDS kr16.as.object)

add_test(NAME kr16.as.vm
  COMMAND kr16 as "${CMAKE_CURRENT_SOURCE_DIR}/integration/vm_hello.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/vm_hello.bin" --raw --base 0x100
)
set_tests_properties(kr16.as.vm PROPERTIES LABELS "integration")

add_test(NAME kr16.vm.raw
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/vm_hello.bin"
    --raw --base 0x100 --stats
)
set_tests_properties(kr16.vm.raw PROPERTIES LABELS "integration"
  DEPENDS kr16.as.vm)

add_test(NAME kr16.size.object
  COMMAND kr16 size "${CMAKE_CURRENT_BINARY_DIR}/asm_min.o"
)
set_tests_properties(kr16.size.object PROPERTIES LABELS "integration"
  DEPENDS kr16.as.object)

add_test(NAME kr16.as.bios
  COMMAND kr16 as "${CMAKE_SOURCE_DIR}/resources/bios.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/bios.bin" --raw --base 0xF100
)
set_tests_properties(kr16.as.bios PROPERTIES LABELS "integration")

add_test(NAME kr16.vm.bios
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/vm_hello.bin"
    --raw --base 0x100 --bios "${CMAKE_CURRENT_BINARY_DIR}/bios.bin"
)
set_tests_properties(kr16.vm.bios PROPERTIES LABELS "integration"
  DEPENDS "kr16.as.vm;kr16.as.bios")

add_test(NAME kr16.as.link_lib
  COMMAND kr16 as "${CMAKE_CURRENT_SOURCE_DIR}/integration/link_lib.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/link_lib.o"
)
set_tests_properties(kr16.as.link_lib PROPERTIES LABELS "integration")

add_test(NAME kr16.as.link_main
  COMMAND kr16 as "${CMAKE_CURRENT_SOURCE_DIR}/integration/link_main.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/link_main.o"
)
set_tests_properties(kr16.as.link_main PROPERTIES LABELS "integration")

add_test(NAME kr16.ld.link
  COMMAND kr16 ld "${CMAKE_CURRENT_BINARY_DIR}/link_lib.o"
    "${CMAKE_CURRENT_BINARY_DIR}/link_main.o"
    -o "${CMAKE_CURRENT_BINARY_DIR}/link.kex"
)
set_tests_properties(kr16.ld.link PROPERTIES LABELS "integration"
  DEPENDS "kr16.as.link_lib;kr16.as.link_main")

add_test(NAME kr16.vm.kex
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/link.kex"
)
set_tests_properties(kr16.vm.kex PROPERTIES LABELS "integration"
  DEPENDS kr16.ld.link)

add_test(NAME kr16.cc.hello
  COMMAND kr16 cc "${CMAKE_CURRENT_SOURCE_DIR}/integration/hello.c"
    -S -o "${CMAKE_CURRENT_BINARY_DIR}/hello.s"
)
set_tests_properties(kr16.cc.hello PROPERTIES LABELS "integration")

add_test(NAME kr16.as.hello
  COMMAND kr16 as "${CMAKE_CURRENT_BINARY_DIR}/hello.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/hello.o"
)
set_tests_properties(kr16.as.hello PROPERTIES LABELS "integration"
  DEPENDS kr16.cc.hello)

add_test(NAME kr16.as.libc
  COMMAND kr16 as "${CMAKE_SOURCE_DIR}/resources/libc.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/libc.o"
)
set_tests_properties(kr16.as.libc PROPERTIES LABELS "integration")

add_test(NAME kr16.ld.hello
  COMMAND kr16 ld "${CMAKE_CURRENT_BINARY_DIR}/libc.o"
    "${CMAKE_CURRENT_BINARY_DIR}/hello.o"
    -o "${CMAKE_CURRENT_BINARY_DIR}/hello.kex"
)
set_tests_properties(kr16.ld.hello PROPERTIES LABELS "integration"
  DEPENDS "kr16.as.libc;kr16.as.hello")

add_test(NAME kr16.vm.hello
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/hello.kex"
)
set_tests_properties(kr16.vm.hello PROPERTIES LABELS "integration"
  DEPENDS kr16.ld.hello PASS_REGULAR_EXPRESSION "Hello, KR16!")
