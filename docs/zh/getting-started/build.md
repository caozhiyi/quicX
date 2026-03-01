# 快速上手：编译与构建指南

欢迎使用 `quicX`！作为一个现代化的 C++ QUIC 和 HTTP/3 协议库，`quicX` 尽可能地保持了依赖的精简。为了确保跨平台编译的顺利进行，请在开始之前阅读本指南。

## 一、 环境要求与前置依赖

无论是 Linux、macOS 还是 Windows 平台，编译 `quicX` 都需要满足以下前置条件：

| 依赖构件 | 版本要求 | 用途与说明 |
| :--- | :--- | :--- |
| **C++ 编译器** | **C++17** 及以上 | 必须，支持 GCC、Clang 或 MSVC (`cl.exe`)。 |
| **CMake** | **≥ 3.16** | 必须，项目核心构建系统。 |
| **BoringSSL** | 最新版 | 必须，作为 Git 子模块引入，用于 TLS 1.3 及底层加密算法。 |
| **多线程库** | POSIX / Windows Native | 必须，(Linux/macOS 下为 `pthread`)。 |
| **GTest** | - | 可选，仅在编译单元测试时需要（通过 CMake 自动拉取，无需手动安装）。 |

> [!IMPORTANT]
> **关于加密库的说明：** `quicX` 强依赖于 BoringSSL（Google 维护的 OpenSSL 分支），因为 QUIC 协议对 TLS 1.3 有特殊的接口要求（如获取加密级别的密钥等）。请**不要**尝试使用系统的 OpenSSL 替换。

---

## 二、 获取源码

因为项目中包含了 BoringSSL 作为子模块，克隆代码时**必须添加 `--recurse-submodules` 标志**：

```bash
# 包含子模块一同克隆
git clone --recurse-submodules https://github.com/caozhiyi/quicX.git

# 进入项目根目录
cd quicX
```

*(如果克隆时忘记加子模块参数，可以在进入目录后执行 `git submodule update --init --recursive` 补救)*

---

## 三、 快速编译

`quicX` 同时支持 CMake 与 Bazel 两种现代构建系统。

### 1. 使用 CMake 构建

采用 CMake 时推荐 "Out-of-source build" 模式，即在一个独立的 `build` 目录中编译，以保持源码树的干净。

#### 1.1 标准编译流程

以下命令将编译库文件、并默认编译出 `example` 下的所有示例：

```bash
# 1. 创建并进入构建目录，进行 CMake 配置 (Release 模式)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON

# 2. 执行编译 (使用多核并行编译加速)
# $(nproc) 适用于 Linux。在 macOS 上可替换为 $(sysctl -n hw.ncpu)
cmake --build build --parallel $(nproc)
```

编译完成后，库文件（如 `libquicx.a`）以及示例可执行文件将放置在 `build/bin/` 目录下。你可以运行一个测试来验证是否成功：

```bash
# 执行单元测试
./build/bin/quicx_utest
```

#### 1.2 CMake 核心构建选项字典

在执行 `cmake -B build ...` 时，你可以通过 `-D<选项>=<ON/OFF>` 来定制你的编译过程。以下是 `quicX` 提供的核心控制开关：

| 选项名称 | 默认值 | 作用解析 |
| :--- | :---: | :--- |
| `CMAKE_BUILD_TYPE` | *空* | 构建类型，推荐设置为 `Release` 或 `Debug`。 |
| `BUILD_EXAMPLES` | `ON` | 是否编译 `example/` 目录下的所有演示程序。首次使用强烈建议开启。 |
| `ENABLE_TESTING` | `ON` | 是否编译单元测试。会通过 FetchContent 自动下载 GTest。 |
| `ENABLE_BENCHMARKS` | `ON` | 是否编译性能基准测试。 |
| `ENABLE_CC_SIMULATOR` | `ON` | 是否编译内置的**拥塞控制模拟器**，对于研究 BBR/CUBIC 算法非常有帮助。 |
| `ENABLE_INTERGRATION` | `ON` | 是否编译本地集成测试对跑工具。 |
| `QUICX_ENABLE_QLOG` | `ON` | **关键指标：** 开启后，允许记录符合 RFC 9001 规范的 `qlog`。这些日志可以直接导入 `qvis` 等可视化工具分析由于拥塞、丢包导致的问题。<br/>*注：开启会在一定程度上影响极限性能。* |

*(如果是进行安全性质疑码排查，还可以开启 `-DENABLE_FUZZING=ON` 结合 `Clang` 编译器进行 libFuzzer 测试。)*

### 2. 使用 Bazel 构建

如果你所在的团队主要使用 Bazel，项目原生提供了基础的 Bazel 支持。

```bash
# 构建所有目标（包含主体库和 example 示例程序）
bazel build //...

# 运行所有的单元测试用例
bazel test //test/...
```

---

## 四、 跨平台编译注意事项

`quicX` 在底层通过统一的网络和线程抽象，抹平了不同操作系统的差异。CI 系统已确保了核心代码在各个平台的可用性。

### Linux / macOS
默认的 `GCC` 或 `Clang` 即可轻松编译。对于 macOS，推荐使用自带的 `Apple Clang`。

### Windows (MSVC)
在 Windows 环境下，你可以使用 Visual Studio (2019/2022) 或者通过开发者命令行进行编译。`quicX` 已去除多余的平台宏定义并适配了纯净的 Windows API。
推荐使用 CMake 结合 MSVC 生成的解决方案直接编译：

```powershell
# 在 Developer PowerShell for VS 中执行：
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## 五、 如何将 quicX 引入你的项目？

### 1. 采用 CMake 集成

最简单的集成方式是借助 CMake 的 `add_subdirectory`：

1. 将 `quicX` 源码放到你的项目目录中（比如 `third_party/quicX/`）。
2. 在你的核心 `CMakeLists.txt` 中添加：

```cmake
# 引入 quicX 目录
add_subdirectory(third_party/quicX)

# 假设你的可执行文件叫 my_app
add_executable(my_app main.cpp)

# 链接 quicX 库以及多线程依赖
target_link_libraries(my_app PRIVATE quicx Threads::Threads)
```

然后，在你的 C++ 代码中：
```cpp
#include "http3/include/if_server.h" 
// 或者引入 #include "quic/include/if_quic_server.h" (仅需传输层)

// 你的逻辑 ...
```

### 2. 采用 Bazel 集成

如果你的自建项目采用 Bazel 系统，你可以在 `WORKSPACE` 或 `MODULE.bazel` 中将 `quicX` 作为 external repository 引入（例如通过 `local_repository` 或 `git_repository`）。

然后在你想使用 `quicX` 的业务目标的 `BUILD.bazel` 文件的 `deps` 中，直接添加对 `@quicX//:quicx` 的依赖即可。
