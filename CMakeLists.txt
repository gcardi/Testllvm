cmake_minimum_required(VERSION 3.20)
project(TestLLVMBasic LANGUAGES CXX)

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
)

llvm_map_components_to_libnames(LLVM_LIBS
  Analysis
  AsmPrinter
  CodeGen
  Core
  InstCombine
  MC
  Passes
  Support
  Target
  TransformUtils
  X86CodeGen
  X86Desc
  X86Info
)

target_link_libraries(basicc PRIVATE ${LLVM_LIBS})

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/basiccw
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:basicc> ${CMAKE_BINARY_DIR}/basiccw
  DEPENDS basicc
  COMMENT "Creating Linux-hosted Windows-targeting driver basiccw"
)

add_custom_target(basiccw_driver ALL
  DEPENDS ${CMAKE_BINARY_DIR}/basiccw
)
