cmake_minimum_required(VERSION 2.8.4)
project(surgeon)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")


link_directories(${LLVM_LIBRARY_DIRS})
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(.)
if(LLVM_BUILD_MAIN_SRC_DIR)
  include_directories(${LLVM_BUILD_MAIN_SRC_DIR}/tools/clang/include)
  include_directories(${LLVM_BUILD_BINARY_DIR}/tools/clang/include)
endif()

add_definitions(
-D__STDC_LIMIT_MACROS
-D__STDC_CONSTANT_MACROS
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fno-rtti -std=c++11 -Wfatal-errors -g")

set(SOURCE_FILES main.cpp JIT.cpp JITMemoryManager.cpp CallGraph.cpp Interactive.cpp Options.cpp)
add_executable(surgeon ${SOURCE_FILES})

set(LLVM_LIBS
  LLVMX86AsmParser 
  LLVMX86Desc
  LLVMX86AsmPrinter 
  LLVMX86Info
  LLVMX86Utils 
  LLVMX86CodeGen
  LLVMObjCARCOpts
  LLVMPasses
  LLVMLTO
  LLVMOrcJIT
  LLVMExecutionEngine
  LLVMRuntimeDyld
  LLVMCoverage
  LLVMipo
  LLVMScalarOpts
  LLVMInstCombine
  LLVMTransformUtils
  LLVMAnalysis
  LLVMTarget
  LLVMCoroutines
  LLVMOption
  LLVMMCParser 
  LLVMMC 
  LLVMObject 
  LLVMBitReader 
  LLVMCore
  LLVMSupport)

 set(CLANG_LIBS
  clangFrontend
  clangSerialization
  clangDriver
  clangParse
  clangSema
  clangAnalysis
  clangAST
  clangBasic
  clangEdit
  clangLex
  clangTooling
  clangCodeGen
 )

target_link_libraries(surgeon
  ${LLVM_LIBS}
  ${CLANG_LIBS}
  ${LLVM_LIBS}
  ${CLANG_LIBS}
)

target_link_libraries(surgeon
    pthread
)

add_custom_command(
OUTPUT surgeon_helpers.bc
DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/helpers.cpp
COMMAND ${LLVM_TOOLS_BINARY_DIR}/clang++ -O3 -fno-exceptions -emit-llvm -c -o surgeon_helpers.bc ${CMAKE_CURRENT_SOURCE_DIR}/helpers.cpp
) 

add_custom_command(
OUTPUT surgeon_inst_helpers.bc
DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/instrumented_helpers.cpp
COMMAND ${LLVM_TOOLS_BINARY_DIR}/clang++ -O3 -fno-exceptions -emit-llvm -c -o surgeon_inst_helpers.bc ${CMAKE_CURRENT_SOURCE_DIR}/instrumented_helpers.cpp
) 

add_custom_target(surgeon_helpers ALL DEPENDS surgeon_helpers.bc surgeon_inst_helpers.bc)
add_dependencies(surgeon surgeon_helpers)