# Sends real TCP flows through ns-3 on a tiny network, and fails unless all finish.
add_test(
  NAME tcp-experiment-smoke
  COMMAND tcp-experiment --nodes=16 --degree=4 --flows=100 --seed=561
)
set_tests_properties(
  tcp-experiment-smoke PROPERTIES LABELS quick PASS_REGULAR_EXPRESSION "completed=100/100"
)
