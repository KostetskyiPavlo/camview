find_package(GTest REQUIRED)

add_executable(camview_tests
    ModeSelectorTest.cpp)

target_link_libraries(camview_tests PRIVATE camview_core camview_options GTest::gtest_main)

include(GoogleTest)
# Discover tests when ctest runs, not at build time: the build then never executes the test
# binary, which matters for sanitizer builds that need a special launch and for
# cross-compilation, where the binary cannot run on the build machine at all.
gtest_discover_tests(camview_tests DISCOVERY_MODE PRE_TEST)