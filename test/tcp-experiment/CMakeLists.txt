# Sends real TCP flows through ns-3 on a tiny network, and fails unless all finish.
add_test(
  NAME tcp-experiment-smoke
  COMMAND tcp-experiment --nodes=16 --degree=4 --flows=100 --seed=561
)
set_tests_properties(
  tcp-experiment-smoke PROPERTIES LABELS quick PASS_REGULAR_EXPRESSION "completed=100/100"
)

add_test(NAME tcp-port-drops
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/check-port-drops.py $<TARGET_FILE:tcp-experiment>)

# Scheduler tests use fake workers and do not launch ns-3 experiments.
add_test(NAME h2-queue
  COMMAND python3 -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p test_h2_queue.py -v)
set_tests_properties(h2-queue PROPERTIES LABELS quick)
