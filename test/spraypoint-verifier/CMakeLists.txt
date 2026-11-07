# Checks the Spraypoint logic against the paper's routing rules (§5), models (§7),
# and benchmarks (§9.1-9.2).  It exits non-zero on a structural violation.
find_package(Threads REQUIRED)

add_executable(spraypoint-verifier checks.cc main.cc oversubscription.cc)
target_link_libraries(spraypoint-verifier PRIVATE spraypoint project_warnings Threads::Threads)

add_test(NAME spraypoint-verifier-quick COMMAND spraypoint-verifier --matchings=0)
set_tests_properties(spraypoint-verifier-quick PROPERTIES LABELS quick)

# Adds the oversubscription benchmark, which takes minutes.
add_test(NAME spraypoint-verifier-full COMMAND spraypoint-verifier)
set_tests_properties(spraypoint-verifier-full PROPERTIES LABELS slow)
