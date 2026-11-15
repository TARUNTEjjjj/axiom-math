#pragma once

// Marks the public API that a shared AxiomMath library exports. A shared library
// is compiled with hidden visibility, so every class or function defined in src/
// and declared in these headers needs it. The attribute also keeps the API
// exported when a parent project builds AxiomMath with hidden visibility.
// A static library exports nothing of its own. CMake defines AXIOM_MATH_STATIC
// for it and for everything that links it, so the API takes the visibility of
// the build that uses it, and a plugin or shared library that links it with
// hidden visibility does not export the API again. Define AXIOM_MATH_STATIC
// when compiling these sources into a static library or program without CMake.
// MinGW and Cygwin export a DLL's symbols without it, and GCC ignores the
// attribute there with a warning, so it is empty on those targets.
#if defined(AXIOM_MATH_STATIC) || defined(_WIN32) || defined(__CYGWIN__)
#define AXIOM_MATH_API
#else
#define AXIOM_MATH_API __attribute__((visibility("default")))
#endif
