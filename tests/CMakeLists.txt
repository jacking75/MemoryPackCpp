# Dependency-free unit tests for the header-only library.
add_executable(memorypack_tests memorypack_tests.cpp)
target_link_libraries(memorypack_tests PRIVATE memorypack::memorypack)

if(MSVC)
    # /utf-8: source/headers are UTF-8; avoids C4819 on non-UTF-8 codepages (e.g. CP949).
    target_compile_options(memorypack_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(memorypack_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME memorypack_tests COMMAND memorypack_tests)
