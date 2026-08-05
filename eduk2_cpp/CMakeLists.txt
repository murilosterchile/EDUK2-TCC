cmake_minimum_required(VERSION 3.16)
cmake_minimum_required(VERSION 3.16)
project(eduk2_cpp LANGUAGES CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall
  -Wextra
)

# GCC/Clang aggressive optimization profile
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(
    -O3
    -march=native
    -mtune=native
    -flto
    -funroll-loops
    -fomit-frame-pointer

  )

  add_link_options(
    -flto
  )
endif()

add_compile_options(-Wall -Wextra)

include_directories(include)

add_library(ukp_common
  src/common/bounds.cpp
  src/common/dominance.cpp
  src/common/io.cpp
  src/common/generator.cpp
  src/common/verify.cpp
)

add_library(ukp_faithful
  src/faithful/faithful_solver.cpp
  src/faithful/critical_sequence.cpp
  src/faithful/incumbent.cpp
  src/faithful/preprocessing.cpp
  src/faithful/eduk2_bounds.cpp
)
target_link_libraries(ukp_faithful ukp_common)

add_library(ukp_optimized
  src/optimized/optimized_solver.cpp
  src/optimized/core_branch_and_bound.cpp
)
target_link_libraries(ukp_optimized ukp_common)

add_executable(ukp_solve src/tools/ukp_solve.cpp)
target_link_libraries(ukp_solve ukp_faithful ukp_optimized ukp_common)

add_executable(ukp_bench src/tools/ukp_bench.cpp)
target_link_libraries(ukp_bench ukp_faithful ukp_optimized ukp_common)

add_executable(ukp_tests tests/test_main.cpp)
target_link_libraries(ukp_tests ukp_faithful ukp_common)
target_compile_definitions(ukp_tests PRIVATE UKP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME faithful_correctness COMMAND ukp_tests)

add_executable(ukp_optimized_regression tests/test_optimized_regression.cpp)
target_link_libraries(ukp_optimized_regression ukp_optimized ukp_common)
target_compile_definitions(ukp_optimized_regression PRIVATE UKP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME optimized_regression COMMAND ukp_optimized_regression)
