#include <dlfcn.h>

#include <cstdio>

// Loads the plugin built from plugin.cpp, a module that links the static
// AxiomMath library and hides its symbols by default. The plugin must work and
// export its entry point, but none of the AxiomMath symbols that it contains.
// These names are mangled the same way with libc++ and libstdc++.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: plugin-loader PLUGIN\n");
        return 2;
    }
    void* const plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (plugin == nullptr) {
        std::fprintf(stderr, "Cannot load %s: %s\n", argv[1], dlerror());
        return 1;
    }
    using Check = int (*)();
    const auto check = reinterpret_cast<Check>(dlsym(plugin, "axiom_embedded_plugin_check"));
    if (check == nullptr || check() != 0) {
        std::fprintf(stderr, "The plugin's axiom_embedded_plugin_check is missing or failed\n");
        return 1;
    }
    const char* const axiom_symbols[] = {
        "_ZNK5axiom4math8Rational3strEv",              // Rational::str() const
        "_ZNK5axiom4math8Rational11denominatorEv",     // Rational::denominator() const, inline
        "_ZN5axiom4mathplERKNS0_8RationalES3_",        // operator+(const Rational&, const Rational&)
        "_ZNK5axiom4math15ExpressionError8positionEv", // ExpressionError::position() const
        "_ZTIN5axiom4math15ExpressionErrorE",          // typeinfo for ExpressionError
        "_ZTVN5axiom4math12OperandErrorE",             // vtable for OperandError
    };
    int status = 0;
    for (const char* name : axiom_symbols) {
        if (dlsym(plugin, name) != nullptr) {
            std::fprintf(stderr, "The plugin exports AxiomMath's %s\n", name);
            status = 1;
        }
    }
    return status;
}
