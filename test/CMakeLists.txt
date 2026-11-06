# One folder per test.  To add a test, create test/<name>/ with a CMakeLists.txt
# like the ones below, and add it to this list.  Label tests that finish in a few
# seconds as "quick", so that ctest -L quick stays fast.
add_subdirectory(flow-size-cdf)
add_subdirectory(shortest-paths)
add_subdirectory(spraypoint-verifier)
add_subdirectory(tcp-bypass)
add_subdirectory(tcp-experiment)
