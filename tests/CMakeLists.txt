find_package(GTest REQUIRED)

add_executable(camview_tests
    ModeSelectorTest.cpp)

target_link_libraries(camview_tests PRIVATE camview_core camview_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(camview_tests)