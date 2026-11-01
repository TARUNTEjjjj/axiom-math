# Verification — 0.1.0

Checked on 1 November 2026 (IST), using Apple Clang 21 on macOS arm64, CMake 4.4.5 and Python 3.9.6.

## Local results

| Check | Result |
| --- | --- |
| Release build, strict warnings treated as errors | Passed |
| Debug build with AddressSanitizer and UndefinedBehaviorSanitizer | Passed |
| CTest: core, library example, CLI help, installed consumer, Python oracle | All five passed in Release and sanitizer builds |
| C++ assertions | Seven groups, 33,198 assertions passed |
| Differential expression tests | 300 generated expressions matched Python `fractions.Fraction` |
| CLI contracts | Exact output/exit checks passed for equality, inequality, explanations, help/version, bad arguments, source pointers and control-byte handling |
| External CMake consumer | Installed headers/library found, linked and executed successfully; includes sanitizer-runtime propagation |

The arithmetic tests include 3,000 deterministic rational pairs with an independent small-integer oracle, integer extrema, normalization, malformed decimals, unsupported syntax and resource boundaries. CLI differential inputs are generated with a fixed seed and use no personal records.

An independent code review checked arithmetic bounds, sign handling, parsing, CLI diagnostics and library consumption. It found a sanitizer-linkage issue for external static-library consumers; the fix propagates the required runtime link options and is covered by the installed-consumer check.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON -DAXIOM_MATH_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

The [CI workflow](../.github/workflows/ci.yml) defines GCC/Linux Release, Clang/Linux sanitizer and Apple Clang/macOS Release jobs. Consult the repository's Actions runs for the result associated with a particular commit; the local record above does not claim a Linux run.

## Limits of this verification

The generated cases are finite samples. Sanitizers and independent oracles provide evidence for the implemented bounds and operations; they are not proofs covering every expression. This release does not include arbitrary precision, symbolic algebra, SAT scoring, a browser integration or performance benchmarks. Native MSVC is outside the documented compiler support scope.
