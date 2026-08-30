cmake_minimum_required(VERSION 3.21)

project(memorypack
    VERSION 0.2.0
    DESCRIPTION "MemoryPack Binary Wire Format compatible C++ header-only library"
    HOMEPAGE_URL "https://github.com/jacking75/MemoryPackCpp"
    LANGUAGES CXX)

# Detect whether this project is built standalone or via add_subdirectory/FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MEMORYPACK_IS_TOP_LEVEL ON)
else()
    set(MEMORYPACK_IS_TOP_LEVEL OFF)
endif()

option(MEMORYPACK_BUILD_TESTS   "Build unit tests"                ${MEMORYPACK_IS_TOP_LEVEL})
option(MEMORYPACK_BUILD_SAMPLES "Build sample clients"            OFF)
option(MEMORYPACK_BUILD_BENCHMARKS "Build benchmarks"             OFF)
option(MEMORYPACK_BUILD_EXAMPLES   "Build example programs"        OFF)
option(MEMORYPACK_INSTALL       "Generate install/package rules"  ${MEMORYPACK_IS_TOP_LEVEL})

# ── Header-only INTERFACE library ───────────────────────────────────────────────
add_library(memorypack INTERFACE)
add_library(memorypack::memorypack ALIAS memorypack)

target_include_directories(memorypack INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(memorypack INTERFACE cxx_std_23)

# ── Tests ───────────────────────────────────────────────────────────────────────
if(MEMORYPACK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Samples ─────────────────────────────────────────────────────────────────────
# The C# server/tool projects are built with `dotnet`, not CMake. This only covers
# the C++ sample clients.
if(MEMORYPACK_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# -- Benchmarks ------------------------------------------------------------------
if(MEMORYPACK_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# -- Examples --------------------------------------------------------------------
if(MEMORYPACK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Install / package config ────────────────────────────────────────────────────
if(MEMORYPACK_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(TARGETS memorypack EXPORT memorypackTargets)

    install(EXPORT memorypackTargets
        FILE memorypackTargets.cmake
        NAMESPACE memorypack::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memorypack)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/memorypackConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/memorypackConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memorypack)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/memorypackConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/memorypackConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/memorypackConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memorypack)
endif()
