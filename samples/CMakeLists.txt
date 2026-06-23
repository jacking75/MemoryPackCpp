# C++ sample clients.
#
# The C# server/tool projects (CSharpServer, ChatServer, tools/cs2cpp) are built
# with `dotnet`, not CMake.
#
#   - CppClient : console test client. Has a POSIX socket fallback, so it builds
#                 on Windows, Linux and macOS.
#   - ChatClient: Win32 GUI client. Windows only.

add_executable(CppClient CppClient/main.cpp)
target_include_directories(CppClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/CppClient)
target_link_libraries(CppClient PRIVATE memorypack::memorypack)
if(WIN32)
    target_link_libraries(CppClient PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(CppClient PRIVATE /utf-8)
endif()

if(WIN32)
    add_executable(ChatClient WIN32 ChatClient/main.cpp)
    target_include_directories(ChatClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ChatClient)
    target_link_libraries(ChatClient PRIVATE memorypack::memorypack ws2_32)
    if(MSVC)
        target_compile_options(ChatClient PRIVATE /utf-8)
    endif()
endif()
