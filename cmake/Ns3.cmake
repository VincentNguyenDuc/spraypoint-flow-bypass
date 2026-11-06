# Provides the pinned ns-3 through find_package.  ns-3 can't be a subproject, since
# its build relies on CMAKE_SOURCE_DIR, so the first configure fetches, builds, and
# installs it under third_party/.  That takes several minutes and happens once.
#
# Pinned so that results stay comparable; do not silently build a later ns-3 tip
# when a collaborator runs the experiment months from now.  Changing the revision
# here rebuilds ns-3.
set(NS3_REPOSITORY https://gitlab.com/nsnam/ns-3-dev.git)
set(NS3_REVISION c3b0d18fc6d6e61b0cc792eb45d8fe7a1db78d08)
set(NS3_MODULES core network point-to-point internet applications)

set(NS3_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/ns-3-dev)
set(NS3_BUILD_DIR ${NS3_SOURCE_DIR}/cmake-cache)
set(NS3_INSTALL_DIR ${PROJECT_SOURCE_DIR}/third_party/ns-3-install)
set(NS3_INSTALLED_REVISION_FILE ${NS3_INSTALL_DIR}/pinned-revision.txt)

set(ns3_installed_revision "")
if(EXISTS ${NS3_INSTALLED_REVISION_FILE})
  file(READ ${NS3_INSTALLED_REVISION_FILE} ns3_installed_revision)
endif()

if(NOT ns3_installed_revision STREQUAL NS3_REVISION)
  find_package(Git REQUIRED)
  if(NOT EXISTS ${NS3_SOURCE_DIR}/ns3)
    message(STATUS "Cloning ns-3")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} clone ${NS3_REPOSITORY} ${NS3_SOURCE_DIR}
      COMMAND_ERROR_IS_FATAL ANY
    )
  endif()
  execute_process(
    COMMAND ${GIT_EXECUTABLE} checkout --quiet --detach ${NS3_REVISION}
    WORKING_DIRECTORY ${NS3_SOURCE_DIR}
    COMMAND_ERROR_IS_FATAL ANY
  )

  # Checkouts from before this build existed link src/ into ns-3's scratch folder,
  # and ns-3 would try to build it.
  if(IS_SYMLINK ${NS3_SOURCE_DIR}/scratch/rng-spraypoint)
    file(REMOVE ${NS3_SOURCE_DIR}/scratch/rng-spraypoint)
  endif()

  message(STATUS "Building and installing ns-3; this takes several minutes")
  string(REPLACE ";" "\;" ns3_module_list "${NS3_MODULES}")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -S ${NS3_SOURCE_DIR} -B ${NS3_BUILD_DIR}
      -DCMAKE_BUILD_TYPE=release
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_INSTALL_PREFIX=${NS3_INSTALL_DIR}
      -DCMAKE_INSTALL_LIBDIR=lib
      -DNS3_ASSERT=OFF
      -DNS3_LOG=OFF
      -DNS3_WARNINGS_AS_ERRORS=OFF
      -DNS3_EXAMPLES=OFF
      -DNS3_TESTS=OFF
      -DNS3_PYTHON_BINDINGS=OFF
      "-DNS3_ENABLED_MODULES=${ns3_module_list}"
    COMMAND_ERROR_IS_FATAL ANY
  )
  execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${NS3_BUILD_DIR} --parallel --target install
    COMMAND_ERROR_IS_FATAL ANY
  )
  file(WRITE ${NS3_INSTALLED_REVISION_FILE} ${NS3_REVISION})
endif()

find_package(
  ns3 REQUIRED
  COMPONENTS ${NS3_MODULES}
  HINTS ${NS3_INSTALL_DIR}/lib/cmake/ns3
  NO_DEFAULT_PATH
)
