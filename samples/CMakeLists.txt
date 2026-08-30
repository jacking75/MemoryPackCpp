# C++ sample clients and servers.
#
# The C# projects (CSharpServer, ChatServer, CsClient, tools/cs2cpp) are built
# with `dotnet`, not CMake.
#
#   - CppClient        : console test client. Has a POSIX socket fallback, so it
#                        builds on Windows, Linux and macOS.
#   - ChatClient       : Win32 GUI client. Windows only.
#   - CppServer        : multi-client C++ game server for the C# CsClient.
#                        Cross-platform.
#   - ChatClientConsole: console client for ChatServer, so Linux and macOS can
#                        run the chat sample too. Cross-platform.
#
# The threaded samples need a thread library on POSIX; on Windows it comes with
# the runtime.
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

add_executable(CppClient CppClient/main.cpp)
target_include_directories(CppClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/CppClient)
target_link_libraries(CppClient PRIVATE memorypack::memorypack)
if(WIN32)
    target_link_libraries(CppClient PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(CppClient PRIVATE /utf-8 /EHsc)
endif()

if(WIN32)
    add_executable(ChatClient WIN32 ChatClient/main.cpp)
    target_include_directories(ChatClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ChatClient)
    target_link_libraries(ChatClient PRIVATE memorypack::memorypack ws2_32)
    if(MSVC)
        target_compile_options(ChatClient PRIVATE /utf-8 /EHsc)
    endif()
endif()

add_executable(CppServer CppServer/main.cpp)
target_include_directories(CppServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/CppServer)
target_link_libraries(CppServer PRIVATE memorypack::memorypack)
if(WIN32)
    target_link_libraries(CppServer PRIVATE ws2_32)
else()
    target_link_libraries(CppServer PRIVATE Threads::Threads)
endif()
if(MSVC)
    target_compile_options(CppServer PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(ChatClientConsole ChatClientConsole/main.cpp)
target_include_directories(ChatClientConsole PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ChatClientConsole)
target_link_libraries(ChatClientConsole PRIVATE memorypack::memorypack)
if(WIN32)
    target_link_libraries(ChatClientConsole PRIVATE ws2_32)
else()
    target_link_libraries(ChatClientConsole PRIVATE Threads::Threads)
endif()
if(MSVC)
    target_compile_options(ChatClientConsole PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
