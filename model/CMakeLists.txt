# FeCIM C++ reference model (Lane C)
#
# Dependency-free C++17 by design (model-verification-design §4.2): the same
# core is later wrapped by pybind11 for the `sim` backend and linked into the
# Verilator co-sim harness, so it must build anywhere with only a compiler.
#
# CI:  cmake -S model -B build && cmake --build build && ./build/model_tests

cmake_minimum_required(VERSION 3.16)
project(fecim_model VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    set(FECIM_WARNINGS /W4 /permissive-)
else()
    set(FECIM_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# ---- core library ---------------------------------------------------------
add_library(fecim_model
    src/primitives.cpp
    src/model_exact.cpp
)
target_include_directories(fecim_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(fecim_model PRIVATE ${FECIM_WARNINGS})

# ---- unit tests -----------------------------------------------------------
# No test framework dependency: tests/test_harness.h is ~60 lines.
add_executable(model_tests
    tests/test_main.cpp
    tests/test_primitives.cpp
    tests/test_model.cpp
)
target_link_libraries(model_tests PRIVATE fecim_model)
target_compile_options(model_tests PRIVATE ${FECIM_WARNINGS})

# Keep the binary at build/model_tests (CI path) on multi-config generators too.
set_target_properties(model_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

enable_testing()
add_test(NAME model_tests COMMAND model_tests)
