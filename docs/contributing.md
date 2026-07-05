# Contributing Guidelines

This document outlines the guidelines for formatting, code quality, and building the project to contribute to `async_engine`.

---

## 1. Code Formatting

All source and header files must comply with the repository's formatting configuration specified in `.clang-format`.

### Style Details
* **Base Style**: LLVM
* **Column Limit**: 100 characters
* **Indent Width**: 4 spaces
* **Short Functions**: Kept on single line only if empty

### Running the Formatter
To check formatting compliance before committing, run the following command from the project root:
```bash
find src include tests examples -type f \( -name "*.cpp" -o -name "*.hpp" \) -print0 | \
  xargs -0 clang-format --dry-run --Werror
```

To automatically format all codebase files, run:
```bash
find src include tests examples -type f \( -name "*.cpp" -o -name "*.hpp" \) -print0 | \
  xargs -0 clang-format -i
```

---

## 2. Static Analysis

`async_engine` enforces static analysis checks using Clang-Tidy to catch memory vulnerabilities, performance issues, and enforce modern C++ practices.

### Checks Configured
* **Checks enabled**: `modernize-*`, `performance-*`, `readability-*`
* **Checks disabled**: `-readability-magic-numbers`, `-readability-identifier-length`, `-modernize-use-trailing-return-type`
* **Warnings as errors**: Enabled globally on CI (`--warnings-as-errors=*`)

### Running Clang-Tidy Locally
Since clang-tidy requires a compilation database (`compile_commands.json`), compile the project first with export commands enabled (which is turned on by default in `CMakeLists.txt`):

```bash
# Generate build files and compilation database
cmake -B build -S . -G Ninja

# Run Clang-Tidy on all source files
find src tests examples -type f -name "*.cpp" -print0 | \
  xargs -0 clang-tidy -p build --warnings-as-errors=*
```

---

## 3. Continuous Integration (CI) Pipeline & Compatibility Workarounds

The repository uses GitHub Actions (`.github/workflows/ci.yml`) to compile and verify all pull requests and commits.

### Environment
* **Platform**: `ubuntu-latest`
* **Compiler**: GCC 15 (`gcc-15`/`g++-15`)
* **Libraries**: `liburing-dev` (version >= 2.14)
* **Standard**: C++26

### Clang-Tidy CI Workarounds
Because GCC 15 and Clang-Tidy (version 18/19) can have system header parsing incompatibilities on standard library headers, our CI pipeline uses the following workarounds to run linting successfully:
1. **Third-Party Exclusions**: The script removes `build/_deps/stdexec-src/.clang-tidy` to prevent clang-tidy from validating third-party Nvidia `stdexec` source files.
2. **GCC Flag Stripping**: Mutates `build/compile_commands.json` using `sed` to strip out compiler flags that are unknown to or cause errors in Clang-Tidy (such as `-fcoroutines`, `-fconcepts-diagnostics-depth=10`, `-Wno-maybe-uninitialized`, and `-Wno-non-template-friend`).
3. **Libc++ Target Standard Library**: Invokes `clang-tidy` using `-stdlib=libc++` (`-extra-arg=-stdlib=libc++`) to bypass header incompatibilities between GCC and Clang.
