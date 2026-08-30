# Header-only library: nothing is compiled, only the headers and the CMake
# package config are installed.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jacking75/MemoryPackCpp
    REF "v${VERSION}"
    SHA512 0
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMEMORYPACK_BUILD_TESTS=OFF
        -DMEMORYPACK_BUILD_SAMPLES=OFF
        -DMEMORYPACK_BUILD_BENCHMARKS=OFF
        -DMEMORYPACK_BUILD_EXAMPLES=OFF
        -DMEMORYPACK_INSTALL=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME memorypack CONFIG_PATH lib/cmake/memorypack)

# An interface-only package has no libraries and no debug artifacts.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
