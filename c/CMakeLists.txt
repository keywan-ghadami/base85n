# The C library, as something other projects can install and link.
#
# Three audiences, one build: `ctest` here, `find_package(base85n)` in a
# consuming CMake project, and vcpkg, which needs both the install rules and a
# build that does not assume GCC. What that costs over the previous six lines
# is the export set and the two templates in cmake/ -- everything else below is
# the same library.
cmake_minimum_required(VERSION 3.15)

# The package's own version, in step with the crate and the Python
# distribution. The wire format all of them implement is specification 0.5.0;
# the patch level is packaging.
project(base85n VERSION 0.5.1 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Defaults that suit whoever is building. On its own: tests and -Werror, the
# way a maintainer wants it. As a subproject or a package build: neither, since
# a consumer did not ask for our tests and a warning from a compiler released
# after this file should not fail their build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BASE85N_STANDALONE ON)
else()
    set(BASE85N_STANDALONE OFF)
endif()
option(BASE85N_BUILD_TESTS "Build the test binary and register it with CTest" ${BASE85N_STANDALONE})
option(BASE85N_WERROR "Treat compiler warnings as errors" ${BASE85N_STANDALONE})

# No STATIC: BUILD_SHARED_LIBS decides, which is what lets one source tree
# serve both a vcpkg static triplet and a dynamic one.
add_library(base85n src/base85n.c)
add_library(base85n::base85n ALIAS base85n)

target_include_directories(base85n PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(base85n PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER include/base85n.h
    # The header declares no __declspec(dllexport), so a shared build on
    # Windows would export nothing without this.
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Per compiler, because -Wall -Wextra -Werror is not a language.
target_compile_options(base85n PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>
    $<$<AND:$<C_COMPILER_ID:GNU,Clang,AppleClang>,$<BOOL:${BASE85N_WERROR}>>:-Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<BOOL:${BASE85N_WERROR}>>:/WX>)

install(TARGETS base85n
    EXPORT base85n-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT base85n-targets
    FILE base85n-targets.cmake
    NAMESPACE base85n::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/base85n)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/base85n-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/base85n-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/base85n)

# The wire format is what a caller depends on and it does not change within
# 0.5.x, so any 0.5.y satisfies a request for 0.5.x.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/base85n-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/base85n-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/base85n-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/base85n)

# For the consumers that do not use CMake, which in C is most of them.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/base85n.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/base85n.pc
    @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/base85n.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(BASE85N_BUILD_TESTS)
    enable_testing()
    add_executable(test_base85n tests/test_base85n.c)
    target_link_libraries(test_base85n PRIVATE base85n)
    target_compile_options(test_base85n PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>
        $<$<AND:$<C_COMPILER_ID:GNU,Clang,AppleClang>,$<BOOL:${BASE85N_WERROR}>>:-Werror>)
    # The shared test vectors live outside this directory; tell the test where
    # they are so it does not depend on the working directory ctest starts it in.
    target_compile_definitions(test_base85n PRIVATE
        BASE85N_TESTVECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../testvectors")
    add_test(NAME test_base85n COMMAND test_base85n)
endif()
