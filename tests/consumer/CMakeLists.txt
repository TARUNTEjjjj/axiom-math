cmake_minimum_required(VERSION 3.20)
project(AxiomMathConsumer LANGUAGES CXX)
find_package(AxiomMath CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE AxiomMath::axiom_math)
# Plugins and many applications compile with hidden visibility; errors thrown
# by a shared AxiomMath library must remain catchable by type there.
set_target_properties(consumer PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
enable_testing()
add_test(NAME external-library-use COMMAND consumer)
# The package imports the CLI only when it is installed alongside the library.
if(TARGET AxiomMath::axiom-math)
    add_test(NAME installed-cli COMMAND AxiomMath::axiom-math eval "1/3 + 1/6")
    set_tests_properties(installed-cli PROPERTIES PASS_REGULAR_EXPRESSION "^1/2\n$")
endif()
