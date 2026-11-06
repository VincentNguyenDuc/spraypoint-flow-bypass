# The Spraypoint core, shared by the tests and the TCP experiment.  It needs no ns-3.
add_library(spraypoint STATIC spraypoint/spraypoint.cc spraypoint/max-flow.cc)
target_include_directories(spraypoint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spraypoint PRIVATE project_warnings)

add_library(flow-size-cdf STATIC tcp/flow-size-cdf.cc)
target_include_directories(flow-size-cdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(flow-size-cdf PRIVATE project_warnings)

add_library(shortest-paths STATIC tcp-bypass/shortest-paths.cc)
target_link_libraries(shortest-paths PUBLIC spraypoint PRIVATE project_warnings)

if(BUILD_TCP_EXPERIMENT)
  include(${PROJECT_SOURCE_DIR}/cmake/Ns3.cmake)

  # Shared by every program built on the TCP experiment.
  add_library(tcp STATIC tcp/tcp-experiment.cc tcp/spraypoint-routing.cc tcp/tcp-options.cc)
  target_link_libraries(
    tcp
    PUBLIC
      spraypoint
      flow-size-cdf
      ns3::core
      ns3::network
      ns3::point-to-point
      ns3::internet
      ns3::applications
    PRIVATE project_warnings
  )

  add_executable(tcp-experiment tcp/main.cc)
  target_link_libraries(tcp-experiment PRIVATE tcp project_warnings)

  add_executable(
    tcp-bypass
    tcp-bypass/main.cc
    tcp-bypass/bypass-experiment.cc
    tcp-bypass/bypass-routing.cc
  )
  target_link_libraries(tcp-bypass PRIVATE tcp shortest-paths project_warnings)
endif()
