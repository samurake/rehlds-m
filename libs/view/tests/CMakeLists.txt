add_executable(view_tests console_input.cpp)
target_link_libraries(view_tests PRIVATE "${LIB_VIEW}" "${LIB_GTEST}")
gtest_discover_tests(view_tests PROPERTIES TIMEOUT 10)
