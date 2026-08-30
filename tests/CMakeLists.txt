# Dependency-free unit and interop tests for the header-only library.

function(memorypack_configure_test target)
    target_link_libraries(${target} PRIVATE memorypack::memorypack)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        # /utf-8: sources are UTF-8; avoids C4819 on non-UTF-8 codepages (e.g. CP949).
        # /EHsc: these tests exercise the exception path, and newer CMake versions
        #        no longer add it by default - without it <chrono> raises C4530,
        #        which /WX then turns into an error.
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion)
    endif()
endfunction()

# ── Unit tests: wire format, buffers, limits, error handling ──────────────────
add_executable(memorypack_tests memorypack_tests.cpp)
memorypack_configure_test(memorypack_tests)
add_test(NAME memorypack_tests COMMAND memorypack_tests)

# ── Interop tests: byte-for-byte against fixtures from real C# MemoryPack ─────
add_executable(memorypack_interop_tests interop_tests.cpp)
memorypack_configure_test(memorypack_interop_tests)
target_compile_definitions(memorypack_interop_tests PRIVATE
    MEMORYPACK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME memorypack_interop_tests COMMAND memorypack_interop_tests)

# `ctest` runs both; CI additionally runs the interop binary with an output
# directory so FormatProbe can validate the C++-produced bytes from C#.
