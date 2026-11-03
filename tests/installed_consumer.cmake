# Exercise the CLI and exported API after moving the installation. This catches
# missing shared-library runtime paths as well as sanitizer runtime propagation.
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Consumer integration failed (${status}):\n${out}\n${err}")
    endif()
endfunction()

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
execute_process(COMMAND "${relocated_prefix}/${AXIOM_BINDIR}/axiom-math${AXIOM_EXECUTABLE_SUFFIX}" eval "1/3 + 1/6"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out STREQUAL "1/2\n" OR NOT err STREQUAL "")
    message(FATAL_ERROR "Relocated installed CLI failed (${status}):\n${out}\n${err}")
endif()
run_checked("${CMAKE_COMMAND}" -S "${AXIOM_SOURCE_DIR}/tests/consumer" -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${relocated_prefix}"
    "-DAxiomMath_DIR=${relocated_prefix}/${AXIOM_LIBDIR}/cmake/AxiomMath"
    "-DCMAKE_CXX_COMPILER=${AXIOM_COMPILER}" "-DCMAKE_BUILD_TYPE=${AXIOM_CONFIG}")
run_checked("${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args})
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" ${ctest_config_args} --output-on-failure)
