add_executable(kr16_tests
  test_main.cpp
  unit/test_version.cpp
  unit/test_cli.cpp
  unit/test_container.cpp
  unit/test_debug_objects.cpp
  unit/test_container_fixtures.cpp
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
