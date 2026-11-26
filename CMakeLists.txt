cmake_minimum_required(VERSION 3.20)
project(AxiomMath VERSION 0.1.0 LANGUAGES CXX)

option(AXIOM_MATH_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AXIOM_MATH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# A top-level build also creates the CLI, the library example, the tests and the
# install rules. A project that embeds AxiomMath with add_subdirectory gets only
# the library unless it turns these options on.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(axiom_math_top_level ON)
else()
    set(axiom_math_top_level OFF)
endif()
include(CMakeDependentOption)
option(AXIOM_MATH_BUILD_CLI "Build the axiom-math command-line tool" ${axiom_math_top_level})
cmake_dependent_option(AXIOM_MATH_BUILD_TESTS "Build the library example and the tests, which also run the CLI"
    ${axiom_math_top_level} "AXIOM_MATH_BUILD_CLI" OFF)
option(AXIOM_MATH_INSTALL "Generate install rules for the library, headers, CMake package and CLI" ${axiom_math_top_level})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)

# GCC/Clang's 128-bit intermediate arithmetic lets the public type stay int64_t
# while reducing products and sums before checking the stored result's range.
# Like src/rational.cpp, the check names both types in __extension__ declarations,
# so GCC's pedantic diagnostic for __int128 cannot fail it when global flags such
# as -pedantic-errors reach this compile.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
__extension__ typedef __int128 Wide;
__extension__ typedef unsigned __int128 UnsignedWide;
int main() {
    Wide value = 1;
    UnsignedWide magnitude = 1;
    return int(value - 1) + int(magnitude - 1);
}" AXIOM_MATH_HAS_INT128)
if(NOT AXIOM_MATH_HAS_INT128)
    # Forget the failure so that the next configure checks again, for example
    # after the compiler or CMAKE_CXX_FLAGS change.
    unset(AXIOM_MATH_HAS_INT128 CACHE)
    message(FATAL_ERROR "AxiomMath requires a compiler/target with __int128 support (GCC or Clang). "
        "The CMake configure log has the compiler output of this check.")
endif()

function(axiom_configure target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    if(AXIOM_MATH_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(AXIOM_MATH_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        # Static archives need their consumers to link the sanitizer runtime.
        target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
endfunction()

add_library(axiom_math src/rational.cpp src/expression.cpp src/tolerance.cpp)
add_library(AxiomMath::axiom_math ALIAS axiom_math)
target_include_directories(axiom_math PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(axiom_math PUBLIC cxx_std_20)
# A shared library exports only the declarations marked AXIOM_MATH_API
# (include/axiom/math/export.hpp). That attribute also keeps them exported when a
# parent project's preset or compile options make hidden visibility the default.
# A static library exports nothing itself: it is compiled with the visibility of
# the project that builds it, and AXIOM_MATH_STATIC leaves the attribute out, so
# a plugin or shared library that links it and hides its symbols hides these too.
get_target_property(axiom_math_type axiom_math TYPE)
if(axiom_math_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(axiom_math PUBLIC AXIOM_MATH_STATIC)
else()
    set_target_properties(axiom_math PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endif()
axiom_configure(axiom_math)

if(AXIOM_MATH_BUILD_CLI)
    add_executable(axiom-math app/main.cpp)
    target_link_libraries(axiom-math PRIVATE AxiomMath::axiom_math)
    axiom_configure(axiom-math)
endif()

# An installed shared-library CLI must find its sibling library without the
# build tree or environment variables. Relative install layouts stay movable.
if(AXIOM_MATH_BUILD_CLI AND BUILD_SHARED_LIBS AND UNIX)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
        set(axiom_install_rpath "${CMAKE_INSTALL_FULL_LIBDIR}")
    else()
        file(RELATIVE_PATH axiom_library_from_bin
            "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
        if(APPLE)
            set(axiom_install_rpath "@loader_path/${axiom_library_from_bin}")
        else()
            set(axiom_install_rpath "$ORIGIN/${axiom_library_from_bin}")
        endif()
    endif()
    set_property(TARGET axiom-math APPEND PROPERTY INSTALL_RPATH "${axiom_install_rpath}")
endif()

if(AXIOM_MATH_BUILD_TESTS)
    add_executable(axiom-example examples/basic.cpp)
    target_link_libraries(axiom-example PRIVATE AxiomMath::axiom_math)
    axiom_configure(axiom-example)
    include(CTest)
endif()

if(AXIOM_MATH_BUILD_TESTS AND BUILD_TESTING)
    add_executable(axiom-tests tests/test_math.cpp)
    target_link_libraries(axiom-tests PRIVATE AxiomMath::axiom_math)
    axiom_configure(axiom-tests)
    add_test(NAME math-core COMMAND axiom-tests)

    add_executable(axiom-property-tests tests/test_cf_properties.cpp)
    target_link_libraries(axiom-property-tests PRIVATE AxiomMath::axiom_math)
    axiom_configure(axiom-property-tests)
    add_test(NAME property-and-fuzz COMMAND axiom-property-tests)
    add_test(NAME library-example COMMAND axiom-example)
    add_test(NAME cli-help COMMAND axiom-math --help)
    # The installation and embedding tests configure nested projects, which must
    # build like this one. Each uses this generator and loads as initial cache
    # this build's toolchain file with the variables it declares for nested
    # projects, build tool, compiler with its launchers, target, external
    # toolchain, sysroot and linker type, flags (including per-configuration
    # ones), standard libraries and Apple architecture settings.
    get_cmake_property(axiom_nested_variables CACHE_VARIABLES)
    list(FILTER axiom_nested_variables INCLUDE REGEX
        "^CMAKE_(CXX_FLAGS|EXE_LINKER_FLAGS|SHARED_LINKER_FLAGS|MODULE_LINKER_FLAGS|CXX_USING_LINKER)_[A-Za-z0-9_]+$")
    set(axiom_nested_settings "")
    foreach(axiom_variable IN ITEMS CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET CMAKE_TOOLCHAIN_FILE
            ${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES} CMAKE_MAKE_PROGRAM
            CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_LAUNCHER CMAKE_CXX_LINKER_LAUNCHER
            CMAKE_CXX_COMPILER_TARGET CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
            CMAKE_SYSROOT CMAKE_SYSROOT_COMPILE CMAKE_SYSROOT_LINK CMAKE_LINKER_TYPE
            CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS
            CMAKE_CXX_STANDARD_LIBRARIES
            CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_SYSROOT CMAKE_CONFIGURATION_TYPES
            ${axiom_nested_variables})
        set(axiom_value "${${axiom_variable}}")
        if(axiom_variable STREQUAL "CMAKE_TOOLCHAIN_FILE" AND axiom_value AND NOT IS_ABSOLUTE "${axiom_value}")
            # CMake reads a relative toolchain file from the top build directory,
            # or else from the top source directory.
            if(EXISTS "${CMAKE_BINARY_DIR}/${axiom_value}")
                set(axiom_value "${CMAKE_BINARY_DIR}/${axiom_value}")
            else()
                set(axiom_value "${CMAKE_SOURCE_DIR}/${axiom_value}")
            endif()
        endif()
        if(NOT axiom_value STREQUAL "")
            string(APPEND axiom_nested_settings "set(${axiom_variable} [==[${axiom_value}]==] CACHE STRING \"\")\n")
        endif()
    endforeach()
    set(axiom_nested_cache "${CMAKE_CURRENT_BINARY_DIR}/nested-build-settings.cmake")
    file(WRITE "${axiom_nested_cache}" "${axiom_nested_settings}")
    # Both tests install, and must ignore DESTDIR and CMAKE_INSTALL_MODE from the
    # environment. CTest sets both, which would otherwise stage the installation
    # elsewhere or install symbolic links instead of files.
    set(axiom_install_environment
        "DESTDIR=${CMAKE_CURRENT_BINARY_DIR}/ignored-destdir" "CMAKE_INSTALL_MODE=ABS_SYMLINK")
    # Embed this source tree as the README describes, in a parent project that
    # builds shared libraries with hidden default visibility, and again as a
    # static library that a plugin of that parent links.
    add_test(NAME embedded-consumer COMMAND ${CMAKE_COMMAND}
        -DAXIOM_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DAXIOM_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        "-DAXIOM_GENERATOR=${CMAKE_GENERATOR}"
        -DAXIOM_INITIAL_CACHE=${axiom_nested_cache}
        -DAXIOM_CONFIG=$<CONFIG>
        -DAXIOM_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/embedded_consumer.cmake)
    set_tests_properties(embedded-consumer PROPERTIES TIMEOUT 120 ENVIRONMENT "${axiom_install_environment}")
    if(NOT AXIOM_MATH_INSTALL)
        message(STATUS "AXIOM_MATH_INSTALL is OFF; skipping the isolated installation test.")
    elseif(NOT IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}" AND
       NOT IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}" AND
       NOT IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
        add_test(NAME installed-consumer COMMAND ${CMAKE_COMMAND}
            -DAXIOM_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DAXIOM_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            "-DAXIOM_GENERATOR=${CMAKE_GENERATOR}"
            -DAXIOM_INITIAL_CACHE=${axiom_nested_cache}
            -DAXIOM_CONFIG=$<CONFIG>
            -DAXIOM_BINDIR=${CMAKE_INSTALL_BINDIR}
            -DAXIOM_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake)
        set_tests_properties(installed-consumer PROPERTIES TIMEOUT 120 ENVIRONMENT "${axiom_install_environment}")
    else()
        message(STATUS "Isolated installation test requires relative install directories; skipping it for absolute destinations.")
    endif()
    find_package(Python3 3.9 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME fraction-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle.py $<TARGET_FILE:axiom-math>)
        add_test(NAME wide-fraction-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/wide_oracle.py $<TARGET_FILE:axiom-math>)
        add_test(NAME integer-power-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/power_oracle.py $<TARGET_FILE:axiom-math>)
        add_test(NAME tolerance-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tolerance_oracle.py $<TARGET_FILE:axiom-math>)
        add_test(NAME cf-json-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cf_json_oracle.py $<TARGET_FILE:axiom-math>)
        set_tests_properties(integer-power-oracle tolerance-oracle cf-json-oracle PROPERTIES TIMEOUT 120)
        set_tests_properties(fraction-oracle wide-fraction-oracle PROPERTIES TIMEOUT 120)
    else()
        message(STATUS "Python 3 unavailable: optional Fraction differential tests will not run.")
    endif()
endif()

if(AXIOM_MATH_INSTALL)
    include(CMakePackageConfigHelpers)
    # Consumers find the package from the installation prefix (CMAKE_PREFIX_PATH).
    # From a prefix, find_package searches lib, lib/<arch> and share on every
    # host, but lib64 only where the platform uses it (not on Debian-family
    # Linux) and no other library directory. Install the package beside the
    # library where that search looks, and in share/cmake/AxiomMath otherwise.
    # That is the literal share directory, not CMAKE_INSTALL_DATADIR, which the
    # search does not follow when it is set to another directory.
    get_property(axiom_lib64_searched GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}" OR CMAKE_INSTALL_LIBDIR STREQUAL "lib" OR
       (CMAKE_LIBRARY_ARCHITECTURE AND CMAKE_INSTALL_LIBDIR STREQUAL "lib/${CMAKE_LIBRARY_ARCHITECTURE}") OR
       (CMAKE_INSTALL_LIBDIR STREQUAL "lib64" AND axiom_lib64_searched AND CMAKE_SIZEOF_VOID_P EQUAL 8))
        set(axiom_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/AxiomMath")
    else()
        set(axiom_package_dir "share/cmake/AxiomMath")
    endif()
    install(TARGETS axiom_math EXPORT AxiomMathTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT AxiomMathTargets FILE AxiomMathTargets.cmake NAMESPACE AxiomMath:: DESTINATION ${axiom_package_dir})
    # The CLI has its own export set. The package imports AxiomMath::axiom-math
    # only when the installed CLI exists, so an installation without it (such as
    # a split development package) still provides AxiomMath::axiom_math.
    if(AXIOM_MATH_BUILD_CLI)
        install(TARGETS axiom-math EXPORT AxiomMathCliTargets RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(EXPORT AxiomMathCliTargets FILE AxiomMathCliTargets.cmake NAMESPACE AxiomMath:: DESTINATION ${axiom_package_dir})
    endif()
    set(AXIOM_MATH_INSTALLED_CLI "${CMAKE_INSTALL_BINDIR}/axiom-math${CMAKE_EXECUTABLE_SUFFIX}")
    configure_package_config_file(cmake/AxiomMathConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfig.cmake
        INSTALL_DESTINATION ${axiom_package_dir} PATH_VARS AXIOM_MATH_INSTALLED_CLI)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfigVersion.cmake VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfigVersion.cmake DESTINATION ${axiom_package_dir})
endif()
