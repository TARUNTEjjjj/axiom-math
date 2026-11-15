# Embed the source tree with add_subdirectory, as the README describes, in a
# parent project that builds shared libraries with hidden default visibility.
# The parent must link and run against AxiomMath::axiom_math, and by default its
# build and installation must contain nothing else from AxiomMath. Embedded as a
# static library, AxiomMath must not be exported by a plugin that links it.
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Embedded build failed (${status}):\n${out}\n${err}")
    endif()
endfunction()

# As in installed_consumer.cmake, the installations below must not follow
# DESTDIR or CMAKE_INSTALL_MODE from the environment.
unset(ENV{DESTDIR})
unset(ENV{CMAKE_INSTALL_MODE})

set(build "${AXIOM_BUILD_DIR}/embedded-build")
set(prefix "${AXIOM_BUILD_DIR}/embedded-install")
set(package_prefix "${AXIOM_BUILD_DIR}/embedded-package")
set(static_build "${AXIOM_BUILD_DIR}/embedded-static-build")
# These are test-owned scratch directories.
file(REMOVE_RECURSE "${build}" "${prefix}" "${package_prefix}" "${static_build}")
# As in installed_consumer.cmake, an empty configuration must omit these options.
set(config_args)
set(ctest_config_args)
if(NOT AXIOM_CONFIG STREQUAL "")
    set(config_args --config "${AXIOM_CONFIG}")
    set(ctest_config_args --build-config "${AXIOM_CONFIG}")
endif()
# The parent is built like this project (see CMakeLists.txt).
set(configure "${CMAKE_COMMAND}" -G "${AXIOM_GENERATOR}" -C "${AXIOM_INITIAL_CACHE}"
    -S "${AXIOM_SOURCE_DIR}/tests/embedded" -B "${build}"
    "-DAXIOM_SOURCE_DIR=${AXIOM_SOURCE_DIR}" "-DCMAKE_BUILD_TYPE=${AXIOM_CONFIG}")
run_checked(${configure})
run_checked("${CMAKE_COMMAND}" --build "${build}" ${config_args})

# The parent builds no AxiomMath programs and gains no testing options.
foreach(program axiom-math axiom-example axiom-tests)
    file(GLOB_RECURSE built "${build}/${program}${AXIOM_EXECUTABLE_SUFFIX}")
    if(built)
        message(FATAL_ERROR "The embedded build also built ${built}")
    endif()
endforeach()
file(STRINGS "${build}/CMakeCache.txt" testing REGEX "^BUILD_TESTING:")
if(testing)
    message(FATAL_ERROR "The embedded build added ${testing} to the parent's cache")
endif()
# Its program links, computes and catches ExpressionError by type.
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${build}" ${ctest_config_args} --output-on-failure)
# Its installation contains only its own program.
run_checked("${CMAKE_COMMAND}" --install "${build}" --prefix "${prefix}" ${config_args})
file(GLOB_RECURSE installed LIST_DIRECTORIES false RELATIVE "${prefix}" "${prefix}/*")
if(NOT installed STREQUAL "bin/embedder${AXIOM_EXECUTABLE_SUFFIX}")
    message(FATAL_ERROR "The parent's installation contains AxiomMath files:\n${installed}")
endif()

# AXIOM_MATH_INSTALL=ON adds the library, headers and CMake package, but no CLI.
run_checked(${configure} -DAXIOM_MATH_INSTALL=ON)
run_checked("${CMAKE_COMMAND}" --build "${build}" ${config_args})
run_checked("${CMAKE_COMMAND}" --install "${build}" --prefix "${package_prefix}" ${config_args})
file(GLOB_RECURSE package_config LIST_DIRECTORIES false "${package_prefix}/AxiomMathConfig.cmake")
file(GLOB_RECURSE header LIST_DIRECTORIES false "${package_prefix}/rational.hpp")
file(GLOB_RECURSE cli LIST_DIRECTORIES false "${package_prefix}/axiom-math${AXIOM_EXECUTABLE_SUFFIX}")
if(NOT package_config OR NOT header OR cli)
    file(GLOB_RECURSE installed LIST_DIRECTORIES false RELATIVE "${package_prefix}" "${package_prefix}/*")
    message(FATAL_ERROR "AXIOM_MATH_INSTALL=ON installed an unexpected set of files:\n${installed}")
endif()

# The same parent with a static AxiomMath: its program links and runs, and a
# plugin that links the library exports nothing from it (tests/embedded).
run_checked("${CMAKE_COMMAND}" -G "${AXIOM_GENERATOR}" -C "${AXIOM_INITIAL_CACHE}"
    -S "${AXIOM_SOURCE_DIR}/tests/embedded" -B "${static_build}"
    "-DAXIOM_SOURCE_DIR=${AXIOM_SOURCE_DIR}" "-DCMAKE_BUILD_TYPE=${AXIOM_CONFIG}" -DBUILD_SHARED_LIBS=OFF)
run_checked("${CMAKE_COMMAND}" --build "${static_build}" ${config_args})
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${static_build}" ${ctest_config_args} --output-on-failure)
