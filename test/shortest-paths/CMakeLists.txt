add_executable(shortest-paths-test shortest-paths-test.cc)
target_link_libraries(shortest-paths-test PRIVATE shortest-paths project_warnings)

add_test(NAME shortest-paths COMMAND shortest-paths-test)
set_tests_properties(shortest-paths PROPERTIES LABELS quick)
