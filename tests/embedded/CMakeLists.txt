cmake_minimum_required(VERSION 3.20)
project(AxiomMathEmbedder LANGUAGES CXX)
if(NOT AXIOM_SOURCE_DIR)
    message(FATAL_ERROR "Set AXIOM_SOURCE_DIR to the AxiomMath source tree.")
endif()
# Many applications and plugins build shared libraries and hide symbols by
# default, through the visibility preset or a compile option. Use both.
# With BUILD_SHARED_LIBS OFF, AxiomMath is a static library instead, which a
# plugin links as well.
option(BUILD_SHARED_LIBS "Embed AxiomMath as a shared library" ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
add_compile_options(-fvisibility=hidden)
if(NOT BUILD_SHARED_LIBS)
    # A static library that a plugin links must be position-independent.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
add_subdirectory("${AXIOM_SOURCE_DIR}" axiom)
# The installed consumer's program checks results and catches ExpressionError by type.
add_executable(embedder ../consumer/main.cpp)
target_link_libraries(embedder PRIVATE AxiomMath::axiom_math)
install(TARGETS embedder)
enable_testing()
add_test(NAME embedded-library-use COMMAND embedder)
# A plugin that links the static library must export its own entry point and
# nothing from AxiomMath. Windows DLLs choose their exports differently.
if(NOT BUILD_SHARED_LIBS AND NOT WIN32 AND NOT CYGWIN)
    add_library(embedded-plugin MODULE plugin.cpp)
    target_link_libraries(embedded-plugin PRIVATE AxiomMath::axiom_math)
    # Without -fvisibility-inlines-hidden, the plugin's own copies of inline
    # AxiomMath functions stay hidden only if it sees the API unmarked too.
    set_target_properties(embedded-plugin PROPERTIES VISIBILITY_INLINES_HIDDEN OFF)
    add_executable(plugin-loader plugin_loader.cpp)
    target_link_libraries(plugin-loader PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME embedded-plugin-exports COMMAND plugin-loader $<TARGET_FILE:embedded-plugin>)
endif()
