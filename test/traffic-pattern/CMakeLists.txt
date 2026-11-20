add_executable(traffic-pattern-test traffic-pattern-test.cc)
target_link_libraries(traffic-pattern-test PRIVATE traffic-pattern project_warnings)

add_test(NAME traffic-pattern COMMAND traffic-pattern-test)
set_tests_properties(traffic-pattern PROPERTIES LABELS quick)
