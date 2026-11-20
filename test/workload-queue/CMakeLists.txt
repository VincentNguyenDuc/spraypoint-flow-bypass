# Tests experiments/workload-queue.py with fake workers; it launches no ns-3 runs.  Some
# cases use Linux process and memory interfaces, so run it on Linux.
add_test(NAME workload-queue
  COMMAND python3 -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p test_workload_queue.py -v)
set_tests_properties(workload-queue PROPERTIES LABELS quick)
