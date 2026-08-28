cmake_minimum_required(VERSION 3.16)
project(eduk2_cpp LANGUAGES CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

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
  add_link_options(-flto)
endif()

include_directories(include)

add_library(ukp_common
  src/common/bounds.cpp
  src/common/dominance.cpp
  src/common/io.cpp
  src/common/generator.cpp
  src/common/verify.cpp
)

set(UKP_FAITHFUL_SOURCES
  src/faithful/faithful_solver.cpp
  src/faithful/critical_sequence.cpp
  src/faithful/incumbent.cpp
  src/faithful/preprocessing.cpp
  src/faithful/eduk2_bounds.cpp
)

# Full is the compatibility build used by the existing CLI, tests and bench.
add_library(ukp_faithful ${UKP_FAITHFUL_SOURCES})
target_compile_definitions(ukp_faithful PUBLIC UKP_STATS_MODE=2)
target_link_libraries(ukp_faithful PUBLIC ukp_common)

# Compile-time telemetry variants. Each executable links exactly one faithful
# implementation, so the optimizer can remove disabled counters and branches.
add_library(ukp_faithful_basic ${UKP_FAITHFUL_SOURCES})
target_compile_definitions(ukp_faithful_basic PUBLIC UKP_STATS_MODE=1)
target_link_libraries(ukp_faithful_basic PUBLIC ukp_common)

add_library(ukp_faithful_none ${UKP_FAITHFUL_SOURCES})
target_compile_definitions(ukp_faithful_none PUBLIC UKP_STATS_MODE=0)
target_link_libraries(ukp_faithful_none PUBLIC ukp_common)

set(UKP_OPTIMIZED_SOURCES
  src/optimized/optimized_solver.cpp
  src/optimized/instance_features.cpp
  src/optimized/critical_sequence.cpp
  src/optimized/incumbent.cpp
  src/optimized/preprocessing.cpp
  src/optimized/eduk2_bounds.cpp
)

add_library(ukp_optimized ${UKP_OPTIMIZED_SOURCES})
target_compile_definitions(ukp_optimized PUBLIC UKP_STATS_MODE=2)
target_link_libraries(ukp_optimized PUBLIC ukp_common)

add_library(ukp_optimized_basic ${UKP_OPTIMIZED_SOURCES})
target_compile_definitions(ukp_optimized_basic PUBLIC UKP_STATS_MODE=1)
target_link_libraries(ukp_optimized_basic PUBLIC ukp_common)

add_library(ukp_optimized_none ${UKP_OPTIMIZED_SOURCES})
target_compile_definitions(ukp_optimized_none PUBLIC UKP_STATS_MODE=0)
target_link_libraries(ukp_optimized_none PUBLIC ukp_common)

function(add_ukp_solve_variant target faithful_target optimized_target)
  add_executable(${target} src/tools/ukp_solve.cpp)
  target_link_libraries(${target}
    PRIVATE ${faithful_target} ${optimized_target} ukp_common)
endfunction()

# Backward-compatible full-telemetry executable.
add_ukp_solve_variant(ukp_solve ukp_faithful ukp_optimized)
# Explicit names for controlled experiments.
add_ukp_solve_variant(ukp_solve_telemetry ukp_faithful ukp_optimized)
add_ukp_solve_variant(ukp_solve_basic ukp_faithful_basic ukp_optimized_basic)
add_ukp_solve_variant(ukp_solve_benchmark ukp_faithful_none ukp_optimized_none)

add_executable(ukp_bench src/tools/ukp_bench.cpp)
target_link_libraries(ukp_bench PRIVATE ukp_faithful ukp_optimized ukp_common)

add_executable(ukp_tests tests/test_main.cpp)
target_link_libraries(ukp_tests PRIVATE ukp_faithful ukp_common)
target_compile_definitions(
  ukp_tests PRIVATE UKP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME faithful_correctness COMMAND ukp_tests)

add_executable(ukp_optimized_regression tests/test_optimized_regression.cpp)
target_link_libraries(
  ukp_optimized_regression PRIVATE ukp_optimized ukp_common)
target_compile_definitions(
  ukp_optimized_regression PRIVATE UKP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME optimized_regression COMMAND ukp_optimized_regression)
