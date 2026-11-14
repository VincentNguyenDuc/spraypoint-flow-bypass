# Also usable in isolation without the repository's ns-3 bootstrap/build logic:
# cmake -S test/tcp-diagnostics -B /tmp/tcp-diagnostics-build
# cmake --build /tmp/tcp-diagnostics-build --parallel 1
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.25)
  project(tcp-diagnostics-check LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  get_filename_component(repository "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  find_package(ns3 REQUIRED COMPONENTS core network internet applications point-to-point
    HINTS "${repository}/third_party/ns-3-install/lib/cmake/ns3" NO_DEFAULT_PATH)
  add_library(tcp-diagnostics-under-test STATIC "${repository}/src/tcp/tcp-diagnostics.cc")
  target_include_directories(tcp-diagnostics-under-test PUBLIC "${repository}/src")
  target_link_libraries(tcp-diagnostics-under-test PUBLIC
    ns3::core ns3::network ns3::internet ns3::applications ns3::point-to-point)
  target_compile_options(tcp-diagnostics-under-test PRIVATE -Wall -Wextra)
  set(diagnostics_library tcp-diagnostics-under-test)
  enable_testing()
else()
  set(diagnostics_library tcp)
endif()

add_executable(tcp-diagnostics-test tcp-diagnostics-test.cc)
target_link_libraries(tcp-diagnostics-test PRIVATE ${diagnostics_library})
target_compile_options(tcp-diagnostics-test PRIVATE -Wall -Wextra)
add_test(NAME tcp-diagnostics-counts COMMAND tcp-diagnostics-test --mock-only)
add_test(NAME tcp-diagnostics-sockets COMMAND tcp-diagnostics-test)
set_tests_properties(tcp-diagnostics-counts tcp-diagnostics-sockets PROPERTIES LABELS quick)
