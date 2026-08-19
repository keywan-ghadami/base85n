# The C implementation, as a vcpkg port.
#
# It builds the `c/` subdirectory of the repository rather than its root: this
# repository holds five implementations of one specification, and only this one
# is C. The test binary is off because a consumer did not ask for it, and it
# reads the shared vectors from a path outside `c/`; -Werror is off because a
# warning from a compiler released after this port should not break somebody's
# build.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO keywan-ghadami/base85n
    # The C library is tagged on its own. The repository releases each
    # implementation separately -- `v*` is the Rust crate, `python-v*` the
    # Python distribution -- because they do not have to move together.
    REF "c-v${VERSION}"
    SHA512 0
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/c"
    OPTIONS
        -DBASE85N_BUILD_TESTS=OFF
        -DBASE85N_WERROR=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME base85n CONFIG_PATH lib/cmake/base85n)
vcpkg_fixup_pkgconfig()

# Headers are installed by both configurations; vcpkg keeps only the release
# copy.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
