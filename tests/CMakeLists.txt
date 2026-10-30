add_executable(kr16_tests
  test_main.cpp
  unit/test_version.cpp
  unit/test_cli.cpp
  unit/test_build.cpp
  unit/test_container.cpp
  unit/test_debug_objects.cpp
  unit/test_container_fixtures.cpp
  unit/test_instruction.cpp
  unit/test_cpu.cpp
  unit/test_assembler.cpp
  unit/test_disassembler.cpp
  unit/test_machine.cpp
  unit/test_vm.cpp
  unit/test_process.cpp
  unit/test_linker.cpp
  unit/test_debug_info.cpp
  unit/test_debug_session.cpp
  unit/test_ui.cpp
  unit/test_compiler.cpp
  unit/test_host.cpp
  unit/test_dap.cpp
  unit/test_resources.cpp
  unit/test_shell.cpp
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

add_test(NAME kr16.as.listing
  COMMAND kr16 as "${CMAKE_CURRENT_SOURCE_DIR}/integration/asm_min.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/asm_min_listed.o"
    -l "${CMAKE_CURRENT_BINARY_DIR}/asm_min.lst"
)
set_tests_properties(kr16.as.listing PROPERTIES LABELS "integration")

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

add_test(NAME kr16.cc.cforu
  COMMAND kr16 build --no-debug
    "${CMAKE_CURRENT_SOURCE_DIR}/kevin/C-for-U.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/C-for-U.kex"
)
set_tests_properties(kr16.cc.cforu PROPERTIES LABELS "integration")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cforu-sandbox")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kevin/C-for-U-input.c"
               "${CMAKE_CURRENT_BINARY_DIR}/cforu-sandbox/C-for-U-input.c"
               COPYONLY)

add_test(NAME kr16.vm.cforu
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/C-for-U.kex"
    --fs-root "${CMAKE_CURRENT_BINARY_DIR}/cforu-sandbox"
    "C-for-U-input.c"
)
set_tests_properties(kr16.vm.cforu PROPERTIES LABELS "integration"
  DEPENDS kr16.cc.cforu
  PASS_REGULAR_EXPRESSION "\\.global fact"
  FAIL_REGULAR_EXPRESSION "C-for-U error")

add_test(NAME kr16.vm.cforu_args
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/C-for-U.kex"
    --fs-root "${CMAKE_CURRENT_BINARY_DIR}/cforu-sandbox"
    -- "C-for-U-input.c" -o "C-for-U-output.s"
)
set_tests_properties(kr16.vm.cforu_args PROPERTIES LABELS "integration"
  DEPENDS kr16.cc.cforu
  PASS_REGULAR_EXPRESSION "C-for-U-input.c"
  FAIL_REGULAR_EXPRESSION "C-for-U error")

add_test(NAME kr16.cforu.generated
  COMMAND ${CMAKE_COMMAND}
    "-DKR16=$<TARGET_FILE:kr16>"
    "-DCFORU_KEX=${CMAKE_CURRENT_BINARY_DIR}/C-for-U.kex"
    "-DCFORU_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cforu-sandbox"
    "-DCFORU_INPUT=C-for-U-input.c"
    "-DGENDIR=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cforu_check.cmake"
)
set_tests_properties(kr16.cforu.generated PROPERTIES LABELS "integration"
  DEPENDS kr16.cc.cforu)

# Heap allocator: the checker is a test-only object, linked explicitly so the
# runtime itself stays small.
add_test(NAME kr16.as.heapcheck
  COMMAND kr16 as "${CMAKE_SOURCE_DIR}/tests/support/heapcheck.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/heapcheck.o"
)
set_tests_properties(kr16.as.heapcheck PROPERTIES LABELS "integration")

add_test(NAME kr16.cc.heap
  COMMAND kr16 cc "${CMAKE_CURRENT_SOURCE_DIR}/integration/heap.c"
    -S -o "${CMAKE_CURRENT_BINARY_DIR}/heap.s"
)
set_tests_properties(kr16.cc.heap PROPERTIES LABELS "integration")

add_test(NAME kr16.as.heap
  COMMAND kr16 as "${CMAKE_CURRENT_BINARY_DIR}/heap.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/heap.o"
)
set_tests_properties(kr16.as.heap PROPERTIES LABELS "integration"
  DEPENDS kr16.cc.heap)

add_test(NAME kr16.ld.heap
  COMMAND kr16 ld "${CMAKE_CURRENT_BINARY_DIR}/heapcheck.o"
    "${CMAKE_CURRENT_BINARY_DIR}/libc.o"
    "${CMAKE_CURRENT_BINARY_DIR}/heap.o"
    -o "${CMAKE_CURRENT_BINARY_DIR}/heap.kex"
)
set_tests_properties(kr16.ld.heap PROPERTIES LABELS "integration"
  DEPENDS "kr16.as.heapcheck;kr16.as.libc;kr16.as.heap")

add_test(NAME kr16.vm.heap
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/heap.kex"
)
set_tests_properties(kr16.vm.heap PROPERTIES LABELS "integration"
  DEPENDS kr16.ld.heap PASS_REGULAR_EXPRESSION "heap ok")

# Randomised churn: malloc/free over 32 slots with the invariant checker after
# every operation, seeded so failures reproduce.
add_test(NAME kr16.as.heap_stress
  COMMAND kr16 as "${CMAKE_SOURCE_DIR}/tests/support/heap_stress.s"
    -o "${CMAKE_CURRENT_BINARY_DIR}/heap_stress.o"
)
set_tests_properties(kr16.as.heap_stress PROPERTIES LABELS "integration")

add_test(NAME kr16.ld.heap_stress
  COMMAND kr16 ld "${CMAKE_CURRENT_BINARY_DIR}/heapcheck.o"
    "${CMAKE_CURRENT_BINARY_DIR}/libc.o"
    "${CMAKE_CURRENT_BINARY_DIR}/heap_stress.o"
    -o "${CMAKE_CURRENT_BINARY_DIR}/heap_stress.kex"
)
set_tests_properties(kr16.ld.heap_stress PROPERTIES LABELS "integration"
  DEPENDS "kr16.as.heapcheck;kr16.as.libc;kr16.as.heap_stress")

add_test(NAME kr16.vm.heap_stress
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/heap_stress.kex"
)
set_tests_properties(kr16.vm.heap_stress PROPERTIES LABELS "integration"
  DEPENDS kr16.ld.heap_stress PASS_REGULAR_EXPRESSION "OK")

# `build` writes intermediates beside its input, so build a copy in the
# binary tree instead of polluting tests/integration.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/integration/hello.c"
               "${CMAKE_CURRENT_BINARY_DIR}/build_hello.c" COPYONLY)

add_test(NAME kr16.build.hello
  COMMAND kr16 build "${CMAKE_CURRENT_BINARY_DIR}/build_hello.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/built_hello.kex"
    --listing=${CMAKE_CURRENT_BINARY_DIR}/built_hello.lst
)
set_tests_properties(kr16.build.hello PROPERTIES LABELS "integration")

add_test(NAME kr16.vm.built_hello
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/built_hello.kex"
)
set_tests_properties(kr16.vm.built_hello PROPERTIES LABELS "integration"
  DEPENDS kr16.build.hello PASS_REGULAR_EXPRESSION "Hello, KR16!")

add_test(NAME kr16.build.run
  COMMAND kr16 build "${CMAKE_CURRENT_BINARY_DIR}/build_hello.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/run_hello.kex" --run
)
set_tests_properties(kr16.build.run PROPERTIES LABELS "integration"
  PASS_REGULAR_EXPRESSION "Hello, KR16!")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/integration/printf_hex.c"
               "${CMAKE_CURRENT_BINARY_DIR}/build_printf_hex.c" COPYONLY)

add_test(NAME kr16.build.printf_hex
  COMMAND kr16 build "${CMAKE_CURRENT_BINARY_DIR}/build_printf_hex.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/printf_hex.kex"
)
set_tests_properties(kr16.build.printf_hex PROPERTIES LABELS "integration")

add_test(NAME kr16.vm.printf_hex
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/printf_hex.kex"
)
set_tests_properties(kr16.vm.printf_hex PROPERTIES LABELS "integration"
  DEPENDS kr16.build.printf_hex
  PASS_REGULAR_EXPRESSION "0 1 f 10 ff fff 1234 ffff -> 255=ff hex Z")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kevin/fileio.c"
               "${CMAKE_CURRENT_BINARY_DIR}/fileio.c" COPYONLY)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fileio-sandbox")

add_test(NAME kr16.fileio.build
  COMMAND kr16 build "${CMAKE_CURRENT_BINARY_DIR}/fileio.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/fileio.kex"
)
set_tests_properties(kr16.fileio.build PROPERTIES LABELS "integration")

add_test(NAME kr16.fileio.run
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/fileio.kex"
    --fs-root "${CMAKE_CURRENT_BINARY_DIR}/fileio-sandbox"
)
set_tests_properties(kr16.fileio.run PROPERTIES LABELS "integration"
  DEPENDS kr16.fileio.build)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kevin/sysfile.c"
               "${CMAKE_CURRENT_BINARY_DIR}/sysfile.c" COPYONLY)

add_test(NAME kr16.sysfile.build
  COMMAND kr16 build "${CMAKE_CURRENT_BINARY_DIR}/sysfile.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/sysfile.kex"
)
set_tests_properties(kr16.sysfile.build PROPERTIES LABELS "integration")

add_test(NAME kr16.sysfile.run
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/sysfile.kex"
    --fs-root "${CMAKE_CURRENT_BINARY_DIR}/fileio-sandbox"
)
set_tests_properties(kr16.sysfile.run PROPERTIES LABELS "integration"
  DEPENDS kr16.sysfile.build)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kevin/args.c"
               "${CMAKE_CURRENT_BINARY_DIR}/args.c" COPYONLY)

add_test(NAME kr16.args.build
  COMMAND kr16 build "${CMAKE_CURRENT_BINARY_DIR}/args.c"
    -o "${CMAKE_CURRENT_BINARY_DIR}/args.kex"
)
set_tests_properties(kr16.args.build PROPERTIES LABELS "integration")

add_test(NAME kr16.args.run
  COMMAND kr16 vm "${CMAKE_CURRENT_BINARY_DIR}/args.kex"
    one "two words" "C:\\Users\\kevin"
)
set_tests_properties(kr16.args.run PROPERTIES LABELS "integration"
  DEPENDS kr16.args.build
  PASS_REGULAR_EXPRESSION "argv\\[3\\]=C:\\\\Users\\\\kevin")

find_program(KR16_NODE_EXECUTABLE node)
if(KR16_NODE_EXECUTABLE)
  add_test(NAME kr16.dap.smoke
    COMMAND "${KR16_NODE_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/scripts/dap-smoke.js"
      "$<TARGET_FILE:kr16>"
      "${CMAKE_CURRENT_BINARY_DIR}/hello.kex"
  )
  set_tests_properties(kr16.dap.smoke PROPERTIES LABELS "integration"
    DEPENDS kr16.ld.hello)
endif()


