# Quick Start: Build & Compilation Guide

Welcome to `quicX`! As a modern C++ QUIC and HTTP/3 protocol library, `quicX` keeps dependencies as minimal as possible. To ensure smooth cross-platform compilation, please read this guide before you begin.

## 1. Prerequisites & Environment Requirements

Whether you are on Linux, macOS, or Windows, building `quicX` requires the following prerequisites:

| Dependency | Version Requirement | Purpose & Notes |
| :--- | :--- | :--- |
| **C++ Compiler** | **C++17** or higher | Required. Supports GCC, Clang, or MSVC (`cl.exe`). |
| **CMake** | **≥ 3.16** | Required. The core build system for the project. |
| **BoringSSL** | Latest | Required. Included as a Git submodule, used for TLS 1.3 and underlying cryptography. |
| **Multithreading Library** | POSIX / Windows Native | Required. (e.g., `pthread` on Linux/macOS). |
| **GTest** | - | Optional. Only needed when building unit tests (automatically pulled via CMake, no manual installation required). |

> [!IMPORTANT]
> **A Note on Cryptography Libraries:** `quicX` heavily relies on BoringSSL (Google's fork of OpenSSL) because the QUIC protocol has special interface requirements for TLS 1.3 (such as extracting encryption secrets). Please **DO NOT** attempt to replace it with the system's OpenSSL.

---

## 2. Getting the Source Code

Since the project includes BoringSSL as a submodule, you **MUST add the `--recurse-submodules` flag** when cloning the code:

```bash
# Clone the repository including submodules
git clone --recurse-submodules https://github.com/caozhiyi/quicX.git

# Enter the project root directory
cd quicX
```

*(If you forgot to add the submodule parameter during cloning, you can remedy this by running `git submodule update --init --recursive` after entering the directory)*

---

## 3. Quick Build

`quicX` natively supports both modern build systems: CMake and Bazel.

### 3.1 Building with CMake

When using CMake, the "Out-of-source build" mode is recommended, which means compiling in a separate `build` directory to keep the source tree clean.

#### 3.1.1 Standard Build Process

The following commands will compile the library files and build all examples under `example/` by default:

```bash
# 1. Create and enter the build directory, run CMake configuration (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON

# 2. Execute compilation (using multi-core parallel build acceleration)
# $(nproc) works on Linux. On macOS, replace with $(sysctl -n hw.ncpu)
cmake --build build --parallel $(nproc)
```

After compilation is complete, the library files (e.g., `libquicx.a`) and example executable files will be placed in the `build/bin/` directory. You can run a test to verify if it was successful:

```bash
# Run unit tests
./build/bin/quicx_utest
```

#### 3.1.2 Core CMake Build Options

When executing `cmake -B build ...`, you can customize your compilation process using `-D<Option>=<ON/OFF>`. Here are the core control switches provided by `quicX`:

| Option Name | Default | Purpose |
| :--- | :---: | :--- |
| `CMAKE_BUILD_TYPE` | *Empty* | Build type, recommended to be `Release` or `Debug`. |
| `BUILD_EXAMPLES` | `ON` | Whether to build all demonstration programs under the `example/` directory. Highly recommended to keep on for the first time. |
| `ENABLE_TESTING` | `ON` | Whether to build unit tests. Will automatically download GTest via FetchContent. |
| `ENABLE_BENCHMARKS` | `ON` | Whether to build performance benchmark tests. |
| `ENABLE_CC_SIMULATOR` | `ON` | Whether to build the built-in **Congestion Control Simulator**, very helpful for studying BBR/CUBIC algorithms. |
| `ENABLE_INTERGRATION` | `ON` | Whether to build local integration testing tools. |
| `QUICX_ENABLE_QLOG` | `ON` | **Key Metric:** When enabled, allows recording `qlog` compliant with RFC 9001. These logs can be imported into visual tools like `qvis` to analyze issues caused by congestion and packet loss.<br/>*Note: Enabling this will affect extreme performance limits.* |

*(For fuzz testing/security patching, you can also enable `-DENABLE_FUZZING=ON` along with the `Clang` compiler for libFuzzer tests.)*

### 3.2 Building with Bazel

If your team primarily uses Bazel, the project provides basic native Bazel support.

```bash
# Build all targets (including the main library and example programs)
bazel build //...

# Run all unit test cases
bazel test //test/...
```

---

## 4. Cross-Platform Compilation Notes

`quicX` abstracts the underlying network and threading to overcome OS differences. The CI system ensures core code availability across all major platforms.

### Linux / macOS
The default `GCC` or `Clang` easily compiles it. For macOS, the built-in `Apple Clang` is recommended.

### Windows (MSVC)
In a Windows environment, you can use Visual Studio (2019/2022) or compile via the Developer Command Prompt. `quicX` has removed redundant platform macros and adapted to clean Windows APIs.
It is recommended to compile directly using the CMake-generated solution for MSVC:

```powershell
# Execute in Developer PowerShell for VS:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## 5. How to Integrate quicX into Your Project?

### 5.1 Integration via CMake

The simplest way is leveraging CMake's `add_subdirectory`:

1. Place the `quicX` source code into your project directory (e.g., `third_party/quicX/`).
2. Add the following to your core `CMakeLists.txt`:

```cmake
# Add the quicX directory
add_subdirectory(third_party/quicX)

# Assume your executable is named my_app
add_executable(my_app main.cpp)

# Link the quicX library and threading dependencies
target_link_libraries(my_app PRIVATE quicx Threads::Threads)
```

Then, in your C++ code:
```cpp
#include "http3/include/if_server.h" 
// Or #include "quic/include/if_quic_server.h" (if you only need the transport layer)

// Your logic ...
```

### 5.2 Integration via Bazel

If your custom project uses the Bazel system, you can introduce `quicX` as an external repository in `WORKSPACE` or `MODULE.bazel` (e.g., via `local_repository` or `git_repository`).

Then, in the `deps` of your business target's `BUILD.bazel` where you want to use `quicX`, just add the dependency on `@quicX//:quicx`.
