cmake_minimum_required(VERSION 3.20)
project(AxiomMath VERSION 0.1.0 LANGUAGES CXX)

option(AXIOM_MATH_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AXIOM_MATH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)

# GCC/Clang's 128-bit intermediate arithmetic lets the public type stay int64_t
# while reducing products and sums before checking the stored result's range.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("int main() { __int128 x = 1; return int(x - 1); }" AXIOM_MATH_HAS_INT128)
if(NOT AXIOM_MATH_HAS_INT128)
    message(FATAL_ERROR "AxiomMath requires a compiler/target with __int128 support (GCC or Clang).")
endif()

function(axiom_configure target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    if(AXIOM_MATH_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(AXIOM_MATH_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        # Static archives need their consumers to link the sanitizer runtime.
        target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
endfunction()

add_library(axiom_math src/rational.cpp src/expression.cpp)
add_library(AxiomMath::axiom_math ALIAS axiom_math)
target_include_directories(axiom_math PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(axiom_math PUBLIC cxx_std_20)
axiom_configure(axiom_math)

add_executable(axiom-math app/main.cpp)
target_link_libraries(axiom-math PRIVATE AxiomMath::axiom_math)
axiom_configure(axiom-math)

# An installed shared-library CLI must find its sibling library without the
# build tree or environment variables. Relative install layouts stay movable.
if(BUILD_SHARED_LIBS AND UNIX)
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

add_executable(axiom-example examples/basic.cpp)
target_link_libraries(axiom-example PRIVATE AxiomMath::axiom_math)
axiom_configure(axiom-example)

include(CTest)
if(BUILD_TESTING)
    add_executable(axiom-tests tests/test_math.cpp)
    target_link_libraries(axiom-tests PRIVATE AxiomMath::axiom_math)
    axiom_configure(axiom-tests)
    add_test(NAME math-core COMMAND axiom-tests)
    add_test(NAME library-example COMMAND axiom-example)
    add_test(NAME cli-help COMMAND axiom-math --help)
    if(NOT IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}" AND
       NOT IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}" AND
       NOT IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
        add_test(NAME installed-consumer COMMAND ${CMAKE_COMMAND}
            -DAXIOM_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DAXIOM_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DAXIOM_COMPILER=${CMAKE_CXX_COMPILER}
            -DAXIOM_CONFIG=$<CONFIG>
            -DAXIOM_BINDIR=${CMAKE_INSTALL_BINDIR}
            -DAXIOM_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DAXIOM_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake)
        set_tests_properties(installed-consumer PROPERTIES TIMEOUT 120)
    else()
        message(STATUS "Isolated installation test requires relative install directories; skipping it for absolute destinations.")
    endif()
    find_package(Python3 3.9 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME fraction-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle.py $<TARGET_FILE:axiom-math>)
        add_test(NAME wide-fraction-oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/wide_oracle.py $<TARGET_FILE:axiom-math>)
        set_tests_properties(fraction-oracle wide-fraction-oracle PROPERTIES TIMEOUT 120)
    else()
        message(STATUS "Python 3 unavailable: optional Fraction differential tests will not run.")
    endif()
endif()

include(CMakePackageConfigHelpers)
install(TARGETS axiom_math axiom-math EXPORT AxiomMathTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT AxiomMathTargets FILE AxiomMathTargets.cmake NAMESPACE AxiomMath:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AxiomMath)
configure_package_config_file(cmake/AxiomMathConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AxiomMath)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfigVersion.cmake VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/AxiomMathConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AxiomMath)
