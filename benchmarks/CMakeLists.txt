# Google Benchmark suite for the header-only MemoryPackCpp library.
#
# Not built by default. Enable it explicitly and always in an optimized build:
#
#   cmake -B build-bench -DMEMORYPACK_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-bench --config Release
#
# Choosing the build type is the caller's job on purpose - this file does not
# force -O2/Release behind your back. Numbers from an unoptimized build are
# meaningless for a header-only library that lives entirely in inlined headers.
# See docs/benchmarks.md for how to run and read the results.

include(FetchContent)

# Google Benchmark's own knobs. Set before MakeAvailable so its CMakeLists picks
# them up; FORCE because they are cache variables owned by that project.
set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
# Extra safety: keep Google Benchmark's own build from failing on warnings and
# from pulling in GoogleTest or doc/install targets we do not need.
set(BENCHMARK_ENABLE_WERROR      OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS       OFF CACHE BOOL "" FORCE)
set(BENCHMARK_USE_BUNDLED_GTEST  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(memorypack_bench memorypack_bench.cpp)

# benchmark_main provides main(); the suite has no main() of its own.
target_link_libraries(memorypack_bench PRIVATE
    memorypack::memorypack
    benchmark::benchmark_main)

target_compile_features(memorypack_bench PRIVATE cxx_std_23)

if(MSVC)
    # /utf-8: sources are UTF-8; avoids C4819 on non-UTF-8 codepages (e.g. CP949).
    # Deliberately no /WX here - Google Benchmark's headers are not warning-clean
    # under the strict flags the unit tests use.
    target_compile_options(memorypack_bench PRIVATE /utf-8)
else()
    # Deliberately no -Werror, for the same reason.
    target_compile_options(memorypack_bench PRIVATE -Wall -Wextra)
endif()
