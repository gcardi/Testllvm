cmake_minimum_required(VERSION 3.20)
project(TestLLVMBasic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(NOT LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL 20)
  message(FATAL_ERROR "This project expects LLVM 20 or newer")
endif()

add_executable(basicc
  src/main.cpp
)

target_include_directories(basicc PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(basicc PRIVATE
  ${LLVM_DEFINITIONS}
  BASIC_RUNTIME_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/basic_runtime.c"
)

llvm_map_components_to_libnames(LLVM_LIBS
  Analysis
  Core
  InstCombine
  Passes
  Support
  TransformUtils
)

target_link_libraries(basicc PRIVATE ${LLVM_LIBS})

add_library(basic_runtime STATIC
  runtime/basic_runtime.c
)

target_include_directories(basic_runtime PUBLIC runtime)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/basiccw
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:basicc> ${CMAKE_BINARY_DIR}/basiccw
  DEPENDS basicc
  COMMENT "Creating Linux-hosted Windows-targeting driver basiccw"
)

add_custom_target(basiccw_driver ALL
  DEPENDS ${CMAKE_BINARY_DIR}/basiccw
)
