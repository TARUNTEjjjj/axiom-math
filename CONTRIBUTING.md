# Contributing

Start with the supported grammar and numeric limits in the README. Keep the public API small and document changes to input syntax or error behavior.

For an arithmetic or parser change, add a regression that demonstrates a real failure. Include integer boundaries, normalization and error positions where relevant. Use an independent calculation or the Python Fraction oracle when checking new arithmetic behavior.

Before a pull request:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAXIOM_MATH_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run a sanitizer build for changes to numeric logic, parsing or memory handling. Keep tests deterministic and use synthetic inputs. A documentation-only change needs relevant link and example checks.

Describe the observed problem, the resulting behavior and the validation performed. Keep commit authorship and dates accurate. Do not commit build directories, editor state, credentials or local personal data. New features listed as future work in the README are proposals, not existing guarantees.
