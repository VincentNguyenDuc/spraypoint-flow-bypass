# Checks the bypass models in docs/bypass-modelling.tex against the routing tables: path
# lengths, edge-disjoint shortest paths, and oversubscription as the bypassed share
# grows.  It needs no ns-3.
find_package(Threads REQUIRED)

add_executable(bypass-model main.cc)
target_link_libraries(bypass-model PRIVATE spraypoint-checks project_warnings Threads::Threads)

add_test(NAME bypass-model-quick
         COMMAND bypass-model --nodes=64 --degree=8 --matchings=2 --shares=0,0.1
                 --outputDir=${CMAKE_CURRENT_BINARY_DIR}/quick)
set_tests_properties(bypass-model-quick PROPERTIES LABELS quick)
