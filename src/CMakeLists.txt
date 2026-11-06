# The Spraypoint core, shared by the tests and the TCP experiment.  It needs no ns-3.
add_library(spraypoint STATIC spraypoint/spraypoint.cc spraypoint/max-flow.cc)
target_include_directories(spraypoint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spraypoint PRIVATE project_warnings)

add_library(flow-size-cdf STATIC tcp/flow-size-cdf.cc)
target_include_directories(flow-size-cdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(flow-size-cdf PRIVATE project_warnings)

if(BUILD_TCP_EXPERIMENT)
  include(${PROJECT_SOURCE_DIR}/cmake/Ns3.cmake)

  add_executable(tcp-experiment tcp/main.cc tcp/tcp-experiment.cc tcp/spraypoint-routing.cc)
  target_link_libraries(
    tcp-experiment
    PRIVATE
      spraypoint
      flow-size-cdf
      project_warnings
      ns3::core
      ns3::network
      ns3::point-to-point
      ns3::internet
      ns3::applications
  )
endif()
