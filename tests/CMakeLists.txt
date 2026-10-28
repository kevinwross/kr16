# Test suite is expanded in M0 after doctest is vendored.
add_test(NAME kr16.version COMMAND kr16 version)
set_tests_properties(kr16.version PROPERTIES LABELS "unit;smoke")
