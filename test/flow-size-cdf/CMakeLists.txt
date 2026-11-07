add_executable(flow-size-cdf-test flow-size-cdf-test.cc)
target_link_libraries(flow-size-cdf-test PRIVATE flow-size-cdf project_warnings)

add_test(NAME flow-size-cdf COMMAND flow-size-cdf-test)
set_tests_properties(flow-size-cdf PROPERTIES LABELS quick)
