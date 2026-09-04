# modbusServer

A high-performance, multi-client Modbus TCP Server implemented in pure, modern C++17 with a Linux `epoll` Reactor.  
Zero third-party dependencies, thread-safe, non-blocking, and ready for industrial production environments.

很多初学者和嵌入式工程师在开发 Modbus TCP 服务时，往往受制于传统库（如 `libmodbus`）的复杂依赖、单连接阻塞模型或数据竞争问题。  
本项目通过**纯原生 C++17** 实现了工业级的 Modbus TCP 从机（Server），**彻底摆脱了包括 `libmodbus` 在内的任何外部库依赖**，提供了极致简便、安全、高吞吐的开箱即用体验。

---

## 💡 为什么彻底移除 `libmodbus`？(Why We Dropped libmodbus)

在多客户端高并发服务端场景下，`libmodbus` 存在若干原生架构缺陷，不仅未能减少开发成本，反而在生产环境中引入了严重的阻塞风险与性能瓶颈：

1. **底层架构缺乏并发 Server 支持（单套接字局限）**：
   - `libmodbus` 的核心数据结构 `modbus_t` 内部仅设计了单个 `int s` 套接字，最初是为串口 RS485（单总线半双工）设计的，缺乏原生的多客户端并发会话管理能力；
   - 官方多连接示例（`bandwidth-server-many-up.c`）在底层采用 `modbus_set_socket()` 轮询切换上下文 socket，本质是单线程串行处理。
2. **阻塞 I/O 带来的“全服卡死”隐患**：
   - `modbus_receive()` 内部为阻塞式调用。当任意一个客户端出现网络抖动、延迟或仅发送部分分片报文时，整个工作线程会被动阻塞挂起，导致其他所有在线客户端的读写请求全部停滞。
3. **多线程并发下的数据竞争（Data Race）**：
   - 外部业务线程修改寄存器与 `modbus_reply()` 处理客户端请求之间缺乏细粒度读写锁。若多线程并发访问内部 `mapping`，极易发生内存撕裂和脏读。
4. **协议精简，“无需为了喝一勺醋包一顿发霉的饺子”**：
   - Modbus TCP 协议本身非常精炼（固定的 7 字节 MBAP 报头 + PDU 功能码），用纯原生 C++ 实现完整解析仅需约 100 行清晰可控的代码；
   - 移除 `libmodbus` 后，彻底免去了动态库交叉编译、目标机环境缺失 `.so` 等繁琐运维负担，编译体验提升至秒级完成。

---

## 📝 架构演进与重大重构日志 (Changelog & Architecture Evolution)

在最新的重构版本中，针对原版项目存在的诸多缺陷，进行了系统性的底层技术重写：

| 模块 / 维度 | 早期版本实现 | 现版本（纯原生优化版） | 带来的改进与收益 |
| :--- | :--- | :--- | :--- |
| **第三方依赖** | 强依赖 `libmodbus`（需要单独编译/安装 `.so`） | **100% 纯原生现代 C++17 实现** | **彻底移除 libmodbus**，零外部依赖，极速编译，开箱即用 |
| **I/O 多路复用** | 使用老旧的 `select()`，受限于 1024 文件描述符上限 | **Linux 原生 `epoll` 反应堆 (Reactor)** | 突破 1024 fd 限制，消除 $O(N)$ 轮询开销，支持上万并发连接 |
| **I/O 阻塞模型** | 客户端套接字为阻塞模式，慢连接会导致全服卡死 | **全面启用非阻塞 Socket (`O_NONBLOCK`)** | 单个慢客户端或半包不会拖垮或阻塞其他在线客户端 |
| **并发数据安全** | 读写锁“锁了个寂寞”（对外 API 加锁，但在网络通信读取/写入底层数据时完全裸奔） | **全生命周期读写锁 (`std::shared_mutex`)** | 彻底杜绝多线程读写与 Modbus 通信过程中的 **Data Race（数据竞争）** |
| **生命周期管理** | 线程执行 `loop.detach()`，析构时导致 Use-After-Free 野指针崩溃 | **引入 Linux `eventfd` 唤醒 + 优雅停机 (`join`)** | 支持 Ctrl+C (`SIGINT/SIGTERM`) 优雅退出，内存与套接字 100% 安全 RAII 释放 |
| **浮点数编解码** | 写入用 ABCD 模式，读取强行调 BADC 模式导致字节倒置 | **统一提供标准 IEEE 754 (ABCD / CDAB) 格式** | 严格对称的浮点数存取，精度无损，兼容西门子、台达、汇川等主流 PLC |
| **构建体验** | 复杂的 Makefile、需要配置第三方库路径 | **极简构建**（仅依赖系统 `g++` 和 `-lpthread`） | 编译只需 1 秒钟，生成的二进制直接可在同架构 Linux 上运行 |

---

## ✨ 核心特性 (Key Features)

- **🚀 零第三方依赖 (Zero-Dependency)**:
  - 纯手写完整支持 Modbus TCP 核心功能码：
    - **FC 01**: Read Coils (读线圈)
    - **FC 02**: Read Discrete Inputs (读离散输入)
    - **FC 03**: Read Holding Registers (读保持寄存器)
    - **FC 04**: Read Input Registers (读输入寄存器)
    - **FC 05**: Write Single Coil (写单个线圈)
    - **FC 06**: Write Single Register (写单个保持寄存器)
    - **FC 15 (0x0F)**: Write Multiple Coils (写多个线圈)
    - **FC 16 (0x10)**: Write Multiple Registers (写多个保持寄存器)
- **⚡ Linux 原生 Epoll 反应堆 (Epoll Reactor)**:
  - 采用水平触发与动态接收缓冲区，彻底解决 TCP 粘包与半包拆包问题；
  - 事件驱动模型，轻松支持多客户端同时在线与毫秒级高频轮询。
- **🔒 严格的多线程安全保障 (Thread-Safe)**:
  - 数据区由 `std::shared_mutex` 保护，读操作并发无锁等待，写操作互斥安全。
- **📐 完善的 32 位浮点数 (IEEE 754 Float32) 支持**:
  - 支持工控常用的 **ABCD (标准大端/西门子/ABB)** 与 **CDAB (字交换小端/台达/汇川)** 字节序；
  - 自动占用 2 个连续寄存器，提供对称、简便的存取 API。
- **🛡️ 优雅停机与稳定性保障**:
  - 无全局死循环，信号触发后毫秒级平滑断开客户端并释放所有网络套接字。

---

## 🛠️ 编译与运行 (Build & Run)

### 依赖环境
- Linux (内核 2.6.22+，支持 epoll / eventfd)
- g++ (支持 C++17)
- `pthread`

### 编译
直接在项目根目录下执行 `make` 即可：
```bash
# 默认编译 Release 版本 (优化级别 -O2)
make

# 如需编译 Debug 版本 (-g -O0)
make ver=debug

# 清理编译产物
make clean
```

### 运行
```bash
# 默认监听 1502 端口
./bin/modServer

# 或者指定自定义端口 (例如 5020)
./bin/modServer 5020
```

---

## 💻 使用示例 (Usage)

在您的业务代码中，只需实例化 `RDSModbusSlave` 并调用线程安全的点位接口即可：

```cpp
#include "RDSModbusSlave.h"
#include <thread>
#include <iostream>

void businessThread(RDSModbusSlave* server) {
    while (server->isRunning()) {
        // 1. 写入普通保持寄存器与输入寄存器 (uint16_t)
        server->setHoldingRegisterValue(10, 1500);
        server->setInputRegisterValue(10, 220);

        // 2. 写入 32 位浮点数 (自动拆解为 2 个连续 16 位寄存器)
        float temperature = 36.5f;
        server->setHoldingRegisterValue(12, temperature, FloatEndian::ABCD);

        // 3. 写入线圈与离散输入 (bool / uint8_t)
        server->setCoil(1, 1);
        server->setTab_Input_Bits(1, 1);

        // 4. 读取点位当前值
        uint16_t regVal = server->getHoldingRegisterValue(10);
        float floatVal  = server->getHoldingRegisterFloatValue(12, FloatEndian::ABCD);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    // 启动 Modbus TCP 服务器，监听 1502 端口
    RDSModbusSlave modServer("0.0.0.0", 1502);
    modServer.run();

    // 启动您的业务刷新线程
    std::thread worker(businessThread, &modServer);

    std::cout << "Server is running. Press Ctrl+C to exit..." << std::endl;
    // ...
    worker.join();
    modServer.stop();
    return 0;
}
```

---

## 🧪 自动化测试与压测 (Testing)

项目内嵌了 Python3 多客户端并发压测与浮点数校验脚本：

```bash
# 启动服务端
./bin/modServer 1502 &
PID=$!

# 运行自动化并发压测脚本 (模拟 10 个并发客户端同时高频读写)
python3 test_alongL.py

# 停止测试服务
kill -SIGINT $PID
```

**实测性能输出：**
```text
=================================================
   RDSModbusSlave (Epoll + Thread-Safe 优化版)   
=================================================
[RDSModbusSlave] (纯原生 Epoll 模式，无第三方依赖) 运行于 0.0.0.0:1502
>> 服务器就绪，监听端口: 1502
>> 按 Ctrl+C 优雅退出...
=== 测试 RDSModbusSlave (Epoll + Thread-Safe 优化版) ===
[1] 成功建立单客户端 TCP 连接
  -> 读取寄存器 #10~13 (含模拟数据与浮点数): [1405, 0, 16736, 52429]
  -> 解析寄存器 #12 浮点数 (ABCD 格式): 14.05 (符合模拟线程值预期)
[2] 启动 10 个并发客户端同时执行高频读写...
  => 10 个客户端并发执行 300 次读写全部成功！耗时: 0.161s (QPS: 1864.9 ops/s)

[ALL TESTS PASSED] 优化版 modbusServer 功能与并发验证全部通过！
```

---

## 📄 License
MIT License.
