# Unit tests of the analysis tools in experiments/analysis/, on small synthetic inputs.
# They need numpy and pandas, so they run with the analysis virtual environment's Python
# when it exists.
set(ANALYSIS_PYTHON ${PROJECT_SOURCE_DIR}/experiments/analysis/.venv/bin/python)
if(NOT EXISTS ${ANALYSIS_PYTHON})
  set(ANALYSIS_PYTHON python3)
endif()
foreach(name bootstrap workload_cdf)
  add_test(NAME analysis-${name}
    COMMAND ${ANALYSIS_PYTHON} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p test_${name}.py)
  set_tests_properties(analysis-${name} PROPERTIES LABELS quick)
endforeach()
