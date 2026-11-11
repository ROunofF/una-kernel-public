# Host unit tests (Google Test)

Native CMake project for logic that runs on the host (Windows/Linux) and in CI.

## Build and run

```bash
cmake -S Tests/Host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

On Windows with Visual Studio:

```bash
cmake -S Tests/Host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --config Debug
ctest --test-dir build-host -C Debug --output-on-failure
```

## Adding tests

Add `*_test.cpp` under `kernel/` or `sdk/` and list it in `Tests/Host/CMakeLists.txt`.
