cmake_minimum_required(VERSION 3.25)
project(spraypoint-bypass LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE -Wall -Wextra)

enable_testing()
add_subdirectory(src)
add_subdirectory(test)

# cmake --build build --target format
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE format_files CONFIGURE_DEPENDS src/*.cc src/*.h test/*.cc test/*.h)
  add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${format_files})
endif()
