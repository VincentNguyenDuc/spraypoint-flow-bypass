add_test(
  NAME tcp-bypass
  COMMAND
    bash ${CMAKE_CURRENT_SOURCE_DIR}/check-bypass.sh
    $<TARGET_FILE:tcp-experiment> $<TARGET_FILE:tcp-bypass>
)
set_tests_properties(tcp-bypass PROPERTIES LABELS quick)
