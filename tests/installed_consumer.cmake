# Exercise the CLI and exported API after moving the installation. This catches
# missing shared-library runtime paths as well as sanitizer runtime propagation.
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Consumer integration failed (${status}):\n${out}\n${err}")
    endif()
endfunction()

# cmake --install reads DESTDIR and CMAKE_INSTALL_MODE from the environment.
# DESTDIR would stage the installation outside the prefix below, and a symlink
# mode would install links to build-tree programs, which still find the library
# through their build-time runtime path. find_package would search a package
# root from the environment before CMAKE_PREFIX_PATH.
unset(ENV{DESTDIR})
unset(ENV{CMAKE_INSTALL_MODE})
unset(ENV{AxiomMath_ROOT})
unset(ENV{AXIOMMATH_ROOT})

set(prefix "${AXIOM_BUILD_DIR}/consumer-install")
set(relocated_prefix "${AXIOM_BUILD_DIR}/consumer-relocated")
set(consumer_build "${AXIOM_BUILD_DIR}/consumer-build")
# These are test-owned scratch directories. A fresh consumer prevents cached
# package locations or obsolete installed libraries from masking a failure.
file(REMOVE_RECURSE "${prefix}" "${relocated_prefix}" "${consumer_build}")
# An empty configuration (a single-config build without CMAKE_BUILD_TYPE) must
# omit these options, because run_checked's unquoted ${ARGV} drops empty values.
set(config_args)
set(ctest_config_args)
if(NOT AXIOM_CONFIG STREQUAL "")
    set(config_args --config "${AXIOM_CONFIG}")
    set(ctest_config_args --build-config "${AXIOM_CONFIG}")
endif()
run_checked("${CMAKE_COMMAND}" --install "${AXIOM_BUILD_DIR}" --prefix "${prefix}" ${config_args})
file(RENAME "${prefix}" "${relocated_prefix}")
set(installed_cli "${relocated_prefix}/${AXIOM_BINDIR}/axiom-math${AXIOM_EXECUTABLE_SUFFIX}")
if(IS_SYMLINK "${installed_cli}")
    message(FATAL_ERROR "The installed CLI is a symbolic link, not an installed file: ${installed_cli}")
endif()
execute_process(COMMAND "${installed_cli}" eval "1/3 + 1/6"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out STREQUAL "1/2\n" OR NOT err STREQUAL "")
    message(FATAL_ERROR "Relocated installed CLI failed (${status}):\n${out}\n${err}")
endif()
# As the README describes, the consumer sets only CMAKE_PREFIX_PATH to find the
# package. It is built like this project (see CMakeLists.txt).
set(consumer_configure "${CMAKE_COMMAND}" -G "${AXIOM_GENERATOR}" -C "${AXIOM_INITIAL_CACHE}"
    -S "${AXIOM_SOURCE_DIR}/tests/consumer" -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${relocated_prefix}" "-DCMAKE_BUILD_TYPE=${AXIOM_CONFIG}")
run_checked(${consumer_configure})
# It must have found this installation, not another one on the system.
file(STRINGS "${consumer_build}/CMakeCache.txt" package_dir REGEX "^AxiomMath_DIR:PATH=")
string(REPLACE "AxiomMath_DIR:PATH=" "" package_dir "${package_dir}")
file(REAL_PATH "${relocated_prefix}" real_prefix)
if(package_dir)
    file(REAL_PATH "${package_dir}" package_dir)
    cmake_path(IS_PREFIX real_prefix "${package_dir}" NORMALIZE found_here)
endif()
if(NOT found_here)
    message(FATAL_ERROR "The consumer found AxiomMath at '${package_dir}', not in ${relocated_prefix}")
endif()
run_checked("${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args})
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" ${ctest_config_args} --output-on-failure)
# The complete installation imports the CLI as AxiomMath::axiom-math.
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" ${ctest_config_args}
    -R "^installed-cli$" --no-tests=error)
# An installation without the CLI, such as a split development package, must
# still provide the library to find_package consumers.
file(REMOVE "${installed_cli}")
run_checked(${consumer_configure})
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" ${ctest_config_args} --output-on-failure)
