# Exercise the exported API from a separate project, including sanitizer
# runtime propagation when the installed archive contains instrumented code.
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Consumer integration failed (${status}):\n${out}\n${err}")
    endif()
endfunction()

set(prefix "${AXIOM_BUILD_DIR}/consumer-install")
set(consumer_build "${AXIOM_BUILD_DIR}/consumer-build")
run_checked("${CMAKE_COMMAND}" --install "${AXIOM_BUILD_DIR}" --prefix "${prefix}" --config "${AXIOM_CONFIG}")
run_checked("${CMAKE_COMMAND}" -S "${AXIOM_SOURCE_DIR}/tests/consumer" -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${prefix}" "-DCMAKE_CXX_COMPILER=${AXIOM_COMPILER}" "-DCMAKE_BUILD_TYPE=${AXIOM_CONFIG}")
run_checked("${CMAKE_COMMAND}" --build "${consumer_build}" --config "${AXIOM_CONFIG}")
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" --build-config "${AXIOM_CONFIG}" --output-on-failure)
