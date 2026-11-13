# Checks the Spraypoint logic against the paper's routing rules (§5), models (§7),
# and benchmarks (§9.1-9.2).  It exits non-zero on a structural violation.
find_package(Threads REQUIRED)

# Shared with test/bypass-model/.
add_library(spraypoint-checks STATIC checks.cc oversubscription.cc)
target_include_directories(spraypoint-checks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spraypoint-checks PUBLIC spraypoint PRIVATE project_warnings)

add_executable(spraypoint-verifier main.cc)
target_link_libraries(spraypoint-verifier PRIVATE spraypoint-checks project_warnings Threads::Threads)

add_test(NAME spraypoint-verifier-quick COMMAND spraypoint-verifier --matchings=0)
set_tests_properties(spraypoint-verifier-quick PROPERTIES LABELS quick)

# Adds the oversubscription benchmark, which takes minutes.
add_test(NAME spraypoint-verifier-full COMMAND spraypoint-verifier)
set_tests_properties(spraypoint-verifier-full PROPERTIES LABELS slow)
