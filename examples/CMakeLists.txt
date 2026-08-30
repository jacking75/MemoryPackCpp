# Self-contained example programs for the header-only MemoryPackCpp library.
#
# Each example is a single .cpp with its own main(), compiles standalone against
# nothing but include/, and exits 0 when everything it demonstrates checks out.
# They double as documentation, so they are heavily commented and print what
# they are doing.
#
# -- Wiring this into the root build --------------------------------------------
# The root CMakeLists.txt needs exactly two additions:
#
#     option(MEMORYPACK_BUILD_EXAMPLES "Build example programs" OFF)
#     ...
#     if(MEMORYPACK_BUILD_EXAMPLES)
#         add_subdirectory(examples)
#     endif()
#
# The option() call below is a harmless duplicate of the first of those
# (option() does nothing for a variable that is already in the cache). It also
# means this directory is safe to add unconditionally, since it returns early
# when the option is off.
#
# -- Building -------------------------------------------------------------------
#     cmake -B build -DMEMORYPACK_BUILD_EXAMPLES=ON
#     cmake --build build
#     ctest --test-dir build -R example --output-on-failure

option(MEMORYPACK_BUILD_EXAMPLES "Build the MemoryPackCpp example programs" OFF)

if(NOT MEMORYPACK_BUILD_EXAMPLES)
    return()
endif()

if(NOT TARGET memorypack::memorypack)
    message(FATAL_ERROR
        "examples/ needs the memorypack::memorypack target. Add this directory "
        "from the root CMakeLists.txt with add_subdirectory(examples), not as a "
        "standalone project.")
endif()

set(MEMORYPACK_EXAMPLE_NAMES
    01_quick_start
    02_nested_and_collections
    03_nullable
    04_union_variant
    05_unmanaged_struct
    06_fixed_buffer
    07_packet_framing
    08_error_handling
    09_version_tolerance
    10_dotnet_types)

foreach(example IN LISTS MEMORYPACK_EXAMPLE_NAMES)
    set(target memorypack_example_${example})

    add_executable(${target} ${example}.cpp)
    target_link_libraries(${target} PRIVATE memorypack::memorypack)
    target_compile_features(${target} PRIVATE cxx_std_23)

    # The binary is named after the example, without the target-name prefix that
    # only exists to keep the CMake target namespace tidy.
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${example})

    # The examples are held to the same warning bar as the unit tests: they are
    # documentation, and documentation that warns teaches the wrong lesson.
    if(MSVC)
        # /utf-8: sources are UTF-8; avoids C4819 on non-UTF-8 codepages (e.g. CP949).
        # /EHsc:  the examples exercise the exception path, and newer CMake no
        #         longer supplies it by default.
        # (The C4702 "unreachable code" false positive that an optimized build
        #  used to raise from core.hpp is now suppressed inside the header, so
        #  consumers do not need /wd4702 here.)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion)
    endif()

    # Every example returns 0 only when all of its own checks passed, so running
    # them under CTest is a real (if coarse) regression test of the public API.
    # Guarded on MEMORYPACK_BUILD_TESTS because that is exactly when the root
    # CMakeLists.txt has called enable_testing().
    if(MEMORYPACK_BUILD_TESTS)
        add_test(NAME example_${example} COMMAND ${target})
    endif()
endforeach()
