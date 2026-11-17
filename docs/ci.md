# CI and CMake walkthrough

The workflow runs four jobs: GCC release, Clang release, Windows MSVC release,
and Linux GCC AddressSanitizer/UndefinedBehaviorSanitizer. It checks correctness,
header consumption, and build integration. It does not publish files or run
timing benchmarks. No downloaded dataset or data-preparation script is needed.

The full local GCC and Clang runs each passed 22 tests. The GCC sanitizer run
also passed 22, with leak detection disabled locally because LeakSanitizer cannot
start under this environment's ptrace tracing. Two external-consumer tests pass.
The hosted sanitizer job retains leak detection. Windows/MSVC and the actual
hosted workflow have not run yet; their first execution happens after pushing.
Workflow syntax passed actionlint 1.7.12. See [validation record](ci-validation.txt).

## How to run it

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Use CMake 3.21+ and a C++20 compiler. Release is explicitly selected at build/test
time because Visual Studio has several configurations in one build directory.
Unix Makefiles normally select the configuration at configure time instead.

For optional accelerated tests, configure with `-DSST_TEST_AVX2=ON`. A generic
CPU probe gates those extra binaries; unsupported hosts clearly report a skip.
A normal top-level configuration has ten tests; a capable accelerated build has
22. Four backends times three suites account for the twelve extra tests.

For sanitizers use a different directory and GCC/Clang:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSST_ENABLE_SANITIZERS=ON -DSST_TEST_AVX2=ON
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize --output-on-failure --no-tests=error --parallel 1
```

In this traced local environment only, set `ASAN_OPTIONS=detect_leaks=0` for the
last command. That disables the leak checker, not address or UB checking. Hosted
CI explicitly uses `detect_leaks=1`. Sanitizer options are private to this
project's executables and are not imposed on consumer applications.

## Workflow: every nonblank line

File: `.github/workflows/ci.yml`. Blank lines only separate related blocks.
YAML indentation creates nesting. `${{ ... }}` is a GitHub expression; matrix
values fill in each compiler job. The matrix has three explicit entries, while
the sanitizer job is independent. `fail-fast: false` preserves compiler failure
evidence; concurrency cancellation still removes superseded runs of the same ref.

| Line | Code | Explanation |
| --- | --- | --- |
| 1 | `name: C++ CI` | Names the workflow shown in the Actions tab. |
| 3 | `on:` | Starts the list of events that trigger this workflow. |
| 4 | `push:` | Adds a trigger for pushed commits. |
| 5 | `branches: [main]` | Limits that push trigger to the main branch. |
| 6 | `pull_request:` | Runs for pull requests as well; no additional branch filter is applied. |
| 7 | `workflow_dispatch:` | Allows a manual run from the Actions interface. |
| 9 | `permissions:` | Starts the permissions requested for the workflow token. |
| 10 | `contents: read` | Grants repository-content read access, without requesting write permission. |
| 12 | `concurrency:` | Starts the rule for grouping overlapping runs. |
| 13 | `group: ci-${{ github.workflow }}-${{ github.ref }}` | Groups runs by workflow and Git ref; the expressions are evaluated by GitHub. |
| 14 | `cancel-in-progress: true` | Cancels an older in-progress run in the same group when a newer one starts. |
| 16 | `jobs:` | Starts the job definitions. |
| 17 | `build:` | Defines the compiler-build job template. |
| 18 | `name: ${{ matrix.name }}` | Uses the current matrix entry as the displayed job name. |
| 19 | `runs-on: ${{ matrix.os }}` | Selects the runner operating system from the matrix entry. |
| 20 | `timeout-minutes: 20` | Stops this job if it exceeds twenty minutes. |
| 21 | `strategy:` | Starts the matrix execution settings. |
| 22 | `fail-fast: false` | Keeps other compiler jobs running when one matrix entry fails. |
| 23 | `matrix:` | Starts the matrix of configurations. |
| 24 | `include:` | Lists explicit configurations rather than taking a Cartesian product. |
| 25 | `- name: Linux GCC` | Starts the Linux GCC configuration and gives it a readable name. |
| 26 | `os: ubuntu-24.04` | Uses the Ubuntu 24.04 runner image for GCC. |
| 27 | `configure: -DCMAKE_CXX_COMPILER=g++ -DSST_TEST_AVX2=ON` | Selects g++ and requests the optional accelerated test matrix. |
| 28 | `- name: Linux Clang` | Starts a separate Linux Clang configuration. |
| 29 | `os: ubuntu-24.04` | Uses the Ubuntu 24.04 runner image for Clang. |
| 30 | `configure: -DCMAKE_CXX_COMPILER=clang++ -DSST_TEST_AVX2=ON` | Selects clang++ and requests the optional accelerated test matrix. |
| 31 | `- name: Windows MSVC` | Starts the Windows MSVC configuration. |
| 32 | `os: windows-2022` | Uses the Windows 2022 runner image with Visual Studio tools. |
| 33 | `configure: -A x64` | Selects a 64-bit Visual Studio build. Accelerated tests remain off. |
| 34 | `steps:` | Starts the ordered steps within each compiler job. |
| 35 | `- name: Check out source` | Labels the source checkout step. |
| 36 | `uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7` | Runs checkout v7 pinned to a verified commit, so tag movement cannot change this reference. |
| 37 | `with:` | Starts the checkout action input settings. |
| 38 | `persist-credentials: false` | Avoids retaining checkout credentials for later Git commands. |
| 39 | `- name: Configure` | Labels the project configuration step. |
| 40 | `run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${{ matrix.configure }}` | Configures the current source directory into build, selects Release, and adds the matrix compiler settings. |
| 41 | `- name: Build` | Labels compilation and linking. |
| 42 | `run: cmake --build build --config Release --parallel 2` | Builds the generated project with at most two parallel tasks. The configuration argument selects Release for Visual Studio. |
| 43 | `- name: Test` | Labels the main CTest run. |
| 44 | `run: ctest --test-dir build -C Release --output-on-failure --no-tests=error --parallel 2` | Runs Release tests from build; prints failed-test output, fails if no tests exist, and runs at most two tests together. |
| 45 | `- name: Configure consumer` | Labels configuration of a separate application consuming this library. |
| 46 | `run: cmake -S tests/consumer -B build/consumer -DCMAKE_BUILD_TYPE=Release ${{ matrix.configure }}` | Configures tests/consumer in its own build directory using the same compiler configuration. |
| 47 | `- name: Build consumer` | Labels the consumer application build. |
| 48 | `run: cmake --build build/consumer --config Release --parallel 2` | Builds that application in Release with at most two parallel tasks. |
| 49 | `- name: Test consumer` | Labels the consumer application test. |
| 50 | `run: ctest --test-dir build/consumer -C Release --output-on-failure --no-tests=error` | Runs the consumer test, showing failures and rejecting an empty test registry. |
| 52 | `sanitizers:` | Defines the independent sanitizer job. |
| 53 | `name: Linux ASan and UBSan` | Names that job in the Actions interface. |
| 54 | `runs-on: ubuntu-24.04` | Uses an Ubuntu 24.04 machine. |
| 55 | `timeout-minutes: 20` | Gives the sanitizer job its own twenty-minute limit. |
| 56 | `env:` | Starts environment variables shared by steps in this job. |
| 57 | `ASAN_OPTIONS: detect_leaks=1` | Enables leak detection on the hosted runner as part of AddressSanitizer. |
| 58 | `steps:` | Starts the sanitizer job steps. |
| 59 | `- name: Check out source` | Labels its source checkout. |
| 60 | `uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7` | Uses the same pinned checkout action as the compiler jobs. |
| 61 | `with:` | Starts its checkout inputs. |
| 62 | `persist-credentials: false` | Disables retained checkout credentials here too. |
| 63 | `- name: Configure` | Labels sanitizer configuration. |
| 64 | `run: >-` | Folds the following indented YAML lines into one shell command; the final newline is stripped. |
| 65 | `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo` | Configures a separate build with optimization and debug information. |
| 66 | `-DCMAKE_CXX_COMPILER=g++ -DSST_ENABLE_SANITIZERS=ON -DSST_TEST_AVX2=ON` | Selects GCC and enables sanitizer instrumentation and optional accelerated checks. |
| 67 | `- name: Build` | Labels the instrumented build. |
| 68 | `run: cmake --build build --parallel 2` | Builds it using at most two compilation tasks. |
| 69 | `- name: Test` | Labels the instrumented test run. |
| 70 | `run: ctest --test-dir build --output-on-failure --no-tests=error --parallel 1` | Runs sanitizer tests serially for clearer diagnostics and lower peak memory; failures and missing tests fail the job. |

## CMake: every nonblank line

File: `CMakeLists.txt`. `${...}` expressions here are CMake variables, rather than
GitHub expressions. An INTERFACE library carries usage requirements without a
compiled library. PRIVATE flags affect project binaries; INTERFACE properties
are inherited by applications that link `sst::static_search_tree`.

| Line | Code | Explanation |
| --- | --- | --- |
| 1 | `cmake_minimum_required(VERSION 3.21)` | Requires CMake 3.21 or newer, including PROJECT_IS_TOP_LEVEL support. |
| 2 | `project(static_search_tree LANGUAGES CXX)` | Declares the project and enables only the C++ language. |
| 4 | `add_library(static_search_tree INTERFACE)` | Creates a header-only INTERFACE target; it produces no library binary. |
| 5 | `add_library(sst::static_search_tree ALIAS static_search_tree)` | Provides the namespaced target applications link against. |
| 6 | `target_include_directories(static_search_tree INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")` | Propagates this header directory to consumers. |
| 7 | `target_compile_features(static_search_tree INTERFACE cxx_std_20)` | Propagates the C++20 requirement to consumers. |
| 9 | `if(PROJECT_IS_TOP_LEVEL)` | Checks whether this is the main project rather than an added dependency. |
| 10 | `include(CTest)` | Enables CTest integration and its BUILD_TESTING option for the main project. |
| 11 | `endif()` | Ends that top-level-only condition. |
| 12 | `option(SST_BUILD_EXAMPLES "Build the usage example" ${PROJECT_IS_TOP_LEVEL})` | Makes examples optional, enabled by default only for a top-level build. |
| 13 | `option(SST_TEST_AVX2 "Test accelerated backends when the host supports them" OFF)` | Adds an opt-in accelerated-test setting, off by default. |
| 14 | `option(SST_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)` | Adds an opt-in sanitizer setting, off by default. |
| 16 | `function(sst_configure_target target)` | Defines a helper for configuring this project's executable targets. |
| 17 | `target_compile_features(${target} PRIVATE cxx_std_20)` | Requests C++20 for that executable, including the isolated copied-header target. |
| 18 | `set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)` | Uses the standard language mode instead of compiler-specific language extensions. |
| 19 | `if(MSVC)` | Chooses the Microsoft compiler option syntax. |
| 20 | `target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)` | Enables Microsoft warnings, stricter conformance, and the correct __cplusplus value. |
| 21 | `else()` | Selects the other supported compiler option syntax. |
| 22 | `target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)` | Enables GCC/Clang warnings, including conversion warnings. |
| 23 | `endif()` | Ends the warning-option selection. |
| 24 | `if(SST_ENABLE_SANITIZERS)` | Applies instrumentation only when explicitly requested. |
| 25 | `if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU\|Clang")` | Rejects unsupported sanitizer/compiler combinations. |
| 26 | `message(FATAL_ERROR "Sanitizer configuration requires GCC or Clang")` | Stops configuration with an explanatory error for that combination. |
| 27 | `endif()` | Ends the compatibility check. |
| 28 | `target_compile_options(${target} PRIVATE -O1 -g -fsanitize=address,undefined` | Adds moderate optimization, debug information, and address/undefined-behavior instrumentation. |
| 29 | `-fno-omit-frame-pointer -fno-sanitize-recover=all)` | Retains frame pointers and makes sanitizer findings fatal. |
| 30 | `target_link_options(${target} PRIVATE -fsanitize=address,undefined)` | Links the required sanitizer runtimes. |
| 31 | `target_compile_definitions(${target} PRIVATE _GLIBCXX_ASSERTIONS)` | Enables extra libstdc++ checks where that library supports them. |
| 32 | `endif()` | Ends the sanitizer-only settings. |
| 33 | `endfunction()` | Ends the helper. PRIVATE settings do not propagate to library consumers. |
| 35 | `if(SST_BUILD_EXAMPLES)` | Checks whether the example was requested. |
| 36 | `add_executable(sst_example examples/simple_search.cpp)` | Creates the example executable. |
| 37 | `target_link_libraries(sst_example PRIVATE sst::static_search_tree)` | Gets the public include path and language requirement through the interface target. |
| 38 | `sst_configure_target(sst_example)` | Adds this project's executable-specific settings. |
| 39 | `endif()` | Ends the example condition. |
| 41 | `if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)` | Builds tests only for a top-level project with BUILD_TESTING enabled. |
| 42 | `find_package(Threads REQUIRED)` | Finds the platform-specific thread support used by the tests. |
| 43 | `function(sst_add_test name source)` | Defines a helper accepting a test name, its first source, and optional extra sources. |
| 44 | `add_executable(sst_${name} ${source} ${ARGN})` | Creates that test executable; ARGN contains additional sources. |
| 45 | `target_link_libraries(sst_${name} PRIVATE sst::static_search_tree Threads::Threads)` | Attaches the header target and platform thread support to it. |
| 46 | `sst_configure_target(sst_${name})` | Applies standard, warning, and optional sanitizer settings. |
| 47 | `add_test(NAME ${name} COMMAND sst_${name})` | Registers the executable with CTest. |
| 48 | `set_tests_properties(${name} PROPERTIES TIMEOUT 180)` | Caps each test execution at 180 seconds. |
| 49 | `endfunction()` | Ends the test-creation helper. |
| 50 | `foreach(name IN ITEMS reference eytzinger stree robustness workload_data batch api)` | Iterates over the seven existing test suites. |
| 51 | `sst_add_test(${name} tests/${name}_test.cpp)` | Builds and registers the corresponding source for each suite. |
| 52 | `endforeach()` | Ends that suite loop. |
| 53 | `sst_add_test(header_integration tests/header_integration_test.cpp tests/header_integration_other.cpp)` | Adds a two-translation-unit test for the public header. |
| 54 | `if(TARGET sst_example)` | Checks that the optional example target exists. |
| 55 | `add_test(NAME example COMMAND sst_example)` | Registers the example as a correctness test. |
| 56 | `endif()` | Ends the example-registration condition. |
| 58 | `# This target can see only the copied header, not the source include directory.` | Documents why the following target deliberately avoids the source include path. |
| 59 | `configure_file(include/stree.hpp copied_header/stree.hpp COPYONLY)` | Copies only the public header into the build tree. |
| 60 | `add_executable(sst_copied_header examples/simple_search.cpp)` | Builds the example again as an isolated header consumer. |
| 61 | `target_include_directories(sst_copied_header PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/copied_header")` | Makes only that copied-header directory visible for its library include. |
| 62 | `sst_configure_target(sst_copied_header)` | Applies executable settings without linking the source interface target. |
| 63 | `add_test(NAME copied_header COMMAND sst_copied_header)` | Registers the isolated consumer with CTest. |
| 65 | `if(SST_TEST_AVX2)` | Starts optional accelerated-test setup. |
| 66 | `if(NOT CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU\|Clang"` | Requires native compilation with a GCC/Clang-family compiler. |
| 67 | `AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64\|AMD64\|amd64\|i.86)$")` | Also requires an x86 processor, rather than trying x86 flags on other architectures. |
| 68 | `include(CheckCXXSourceRuns)` | Loads CMake's compile-and-run probe helper. |
| 69 | `check_cxx_source_runs("int main() { __builtin_cpu_init();` | Starts a baseline-instruction executable that initializes compiler CPU detection. |
| 70 | `return __builtin_cpu_supports(\"avx2\") && __builtin_cpu_supports(\"popcnt\") ? 0 : 1; }"` | Returns success only when AVX2 and POPCNT are both available. |
| 71 | `SST_HOST_HAS_AVX2_POPCNT)` | Stores/caches that probe result under this variable name. |
| 72 | `if(SST_HOST_HAS_AVX2_POPCNT)` | Adds instruction-specific binaries only when the probe passed. |
| 73 | `foreach(backend IN ITEMS avx2 mask_disabled software_popcount prefetch_disabled)` | Iterates over four accelerated/control configurations. |
| 74 | `foreach(name IN ITEMS stree batch api)` | Runs the tree, batch, and public API suites in each configuration. |
| 75 | `sst_add_test(${name}_${backend} tests/${name}_test.cpp)` | Creates a separate executable/test for that suite and backend. |
| 76 | `target_compile_options(sst_${name}_${backend} PRIVATE -mavx2 -mpopcnt)` | Enables AVX2 and hardware population count only for that test target. |
| 77 | `if(backend STREQUAL "mask_disabled")` | Selects the explicit-mask-disabled control configuration. |
| 78 | `target_compile_definitions(sst_${name}_${backend} PRIVATE SST_DISABLE_MASK_SIMD)` | Disables the explicit mask backend for that target. |
| 79 | `elseif(backend STREQUAL "software_popcount")` | Selects the software-population-count control. |
| 80 | `target_compile_options(sst_${name}_${backend} PRIVATE -mno-popcnt)` | Overrides hardware population count with its software implementation. |
| 81 | `elseif(backend STREQUAL "prefetch_disabled")` | Selects the hints-disabled control. |
| 82 | `target_compile_definitions(sst_${name}_${backend} PRIVATE SST_DISABLE_PREFETCH)` | Compiles explicit prefetch hints out of that target. |
| 83 | `endif()` | Ends the backend-specific option selection. |
| 84 | `endforeach()` | Ends the loop over the three suites. |
| 85 | `endforeach()` | Ends the loop over the four backend configurations. |
| 86 | `else()` | Handles a supported compiler/platform whose CPU probe failed. |
| 87 | `message(WARNING "AVX2/POPCNT unavailable: accelerated tests skipped; generic tests remain enabled")` | Clearly reports skipped accelerated tests while keeping generic tests enabled. |
| 88 | `endif()` | Ends the successful-probe condition. |
| 89 | `else()` | Handles cross-compilation, unsupported compilers, or non-x86 platforms. |
| 90 | `message(WARNING "Accelerated tests require native GCC/Clang on x86; generic tests remain enabled")` | Reports why accelerated tests were not created; portable tests remain. |
| 91 | `endif()` | Ends the platform/compiler check. |
| 92 | `endif()` | Ends optional accelerated-test setup. |
| 93 | `endif()` | Ends the top-level test block. |

## External consumer

`tests/consumer/CMakeLists.txt` is intentionally separate from the root project:

| Line | Meaning |
| --- | --- |
| 1 | Requires the same CMake baseline. |
| 2 | Creates an independent C++ application project. |
| 4 | Adds the library as a subdirectory with its own binary directory. |
| 5 | Builds the example application without setting its include path or language standard manually. |
| 6 | Links the public interface target, which supplies both requirements. |
| 8 | Enables tests in this application project. |
| 9 | Registers its executable as a test. |

The library sees it is a dependency, so it does not create its own tests or
examples by default. The CI builds/tests this consumer separately on all three
compilers. The two small header-integration sources check repeated inclusion,
linking across translation units, duplicates, and maximum-value positions.

## After pushing

Open the repository's Actions tab, select C++ CI, and inspect each compiler and
sanitizer job. Configure failures, compilation errors, failed tests, and missing
tests produce failed checks. Requiring these checks before merging is a separate
GitHub branch-rule setting; this change does not modify repository settings.

The checkout action is pinned to a verified v7 commit. Updating it is an explicit
dependency change. No credentials are needed in the repository, no output is
committed automatically, and no deployment/publishing step is present. CI uses
synthetic fixtures; larger real-data benchmarks remain manual and timing-based
regressions are not gated on variable hosted-runner performance.
