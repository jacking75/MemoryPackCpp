# Dependency-free unit and interop tests for the header-only library.

if(MSVC)
    # Some CMake versions/configs (observed: RelWithDebInfo) default
    # CMAKE_CXX_FLAGS(_<CONFIG>) to include /EHsc for MSVC; others (observed:
    # Release) do not. Strip it unconditionally so every target below controls
    # its own exception model explicitly via memorypack_configure_test() or its
    # own target_compile_options(), instead of silently depending on which
    # config happened to omit the default - memorypack_tests_noexcept's
    # /EHs-c- would otherwise fight an inherited /EHsc (D9025, harmless but
    # noisy, and fragile if a future MSVC/CMake pairing turns it fatal).
    foreach(_mp_flags_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
                          CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REPLACE "/EHsc" "" ${_mp_flags_var} "${${_mp_flags_var}}")
    endforeach()
endif()

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

    if(MEMORYPACK_SANITIZE)
        if(MSVC)
            # MSVC ships only AddressSanitizer. /RTC1 is incompatible with it, and
            # CMake's Debug flags add /RTC1 - build ASan in RelWithDebInfo.
            if(NOT MEMORYPACK_SANITIZE MATCHES "address")
                message(FATAL_ERROR "MSVC supports only -DMEMORYPACK_SANITIZE=address")
            endif()
            target_compile_options(${target} PRIVATE /fsanitize=address)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        else()
            target_compile_options(${target} PRIVATE -fsanitize=${MEMORYPACK_SANITIZE} -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=${MEMORYPACK_SANITIZE})
        endif()
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

# -- The same unit tests, built without C++ exceptions -------------------------
# Unreal Engine and some console toolchains disable exceptions; the library then
# reports through the reader/writer error state and std::expected. Without this
# target that path regresses silently.
#
# Only the unit tests: interop_tests.cpp uses <filesystem>, which MSVC's STL does
# not support with _HAS_EXCEPTIONS=0.
#
# Does not use memorypack_configure_test(): that helper adds /EHsc, which
# conflicts with /EHs-c-  below (D9025).
add_executable(memorypack_tests_noexcept memorypack_tests.cpp)
target_link_libraries(memorypack_tests_noexcept PRIVATE memorypack::memorypack)
target_include_directories(memorypack_tests_noexcept PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(memorypack_tests_noexcept PRIVATE MEMORYPACK_NO_EXCEPTIONS)
if(MSVC)
    target_compile_definitions(memorypack_tests_noexcept PRIVATE _HAS_EXCEPTIONS=0)
    target_compile_options(memorypack_tests_noexcept PRIVATE
        /W4 /WX /permissive- /utf-8 /EHs-c-)
else()
    target_compile_options(memorypack_tests_noexcept PRIVATE -fno-exceptions
        -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion)
endif()
add_test(NAME memorypack_tests_noexcept COMMAND memorypack_tests_noexcept)

# -- Fuzz corpus replay -------------------------------------------------------
# The fuzz harness itself needs clang+libFuzzer, which is not available on every
# machine. Replaying the committed corpus needs neither, so the regression value
# of past crashes is always in `ctest`.
add_executable(memorypack_fuzz_replay fuzz/fuzz_replay.cpp fuzz/fuzz_deserialize.cpp)
memorypack_configure_test(memorypack_fuzz_replay)
add_test(NAME memorypack_fuzz_replay
         COMMAND memorypack_fuzz_replay "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus")

# `ctest` runs all of the above; see tools/verify.ps1 for the full local
# verification pipeline (fixtures, cs2cpp drift, samples E2E, ASan) - there is
# no hosted CI in this repository.
