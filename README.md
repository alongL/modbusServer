# modbusServer

A high-performance, multi-client Modbus TCP Server implemented with `libmodbus` and Linux `epoll` Reactor.  
Easy to use, thread-safe, and ready for industrial production environments.

很多初学者和嵌入式工程师在使用 `libmodbus` 时，常常被底层的复杂机制困扰。本项目对 `libmodbus` 进行了现代 C++ 工业级封装，彻底解决了多客户端并发冲突、I/O 阻塞、浮点数字节序以及线程安全问题，开箱即用。

---

## ✨ 核心特性 (Key Features)

- **🚀 Linux 原生 Epoll 反应堆 (Epoll Reactor)**:
  - 彻底淘汰了老旧且有 1024 文件描述符限制的 `select()`；
  - 采用 $O(1)$ 事件驱动模型，轻松支持成百上千个客户端高并发同时在线。
- **🔒 严格的多线程安全保障 (Thread-Safe)**:
  - 底层基于 `std::shared_mutex`（读写锁）对点位寄存器数据区全生命周期受控保护；
  - 彻底解决了网络通信线程在调用 `modbus_reply` 时与外部业务线程写入发生 **Data Race（数据竞争）** 的经典 Bug。
- **⚡ 非阻塞 Socket (Non-Blocking I/O)**:
  - 客户端 Socket 统一设置为非阻塞，防止慢连接或恶意半包导致整个单线程服务挂死。
- **📐 完善的 32 位浮点数 (IEEE 754 Float32) 支持**:
  - 支持工控常用的 **ABCD (标准大端/西门子)** 与 **CDAB (字交换小端/台达/汇川)** 字节序；
  - 存取接口严格对称，消除以往版本中写入 ABCD 却用 BADC 解析的字节错乱问题。
- **🛡️ 优雅停机与 RAII 资源管理**:
  - 移除不安全的 `loop.detach()`，通过 Linux `eventfd` 实现毫秒级异步事件唤醒；
  - 接入 `SIGINT / SIGTERM`（Ctrl+C），优雅等待工作线程退出并释放资源，彻底杜绝析构后的 Use-After-Free 野指针崩溃。
- **📦 零动态依赖部署**:
  - 支持直接静态链接 `libmodbus.a`，打出的独立二进制可执行文件拷到任何 Linux 机器上直接跑，无需在目标机上运行 `sudo apt install libmodbus-dev`。

---

## 🛠️ 编译与运行 (Build & Run)

### 依赖环境
- Linux (内核 2.6.22+，支持 epoll / eventfd)
- g++ (支持 C++17)
- `libmodbus` (支持系统全局安装，或使用仓库内静态链接)

### 编译
直接在项目根目录下执行 `make`：
```bash
# 默认编译 Release 版本
make

# 如需编译 Debug 版本
make ver=debug

# 清理编译产物
make clean
```

### 运行
```bash
# 默认监听 1502 端口
./bin/modServer

# 或者指定自定义端口
./bin/modServer 5020
```

---

## 💻 使用示例 (Usage)

在您的业务线程中，直接调用安全的点位读写接口：

```cpp
#include "RDSModbusSlave.h"
#include <thread>
#include <iostream>

void businessThread(RDSModbusSlave* server) {
    while (server->isRunning()) {
        // 1. 写入普通保持寄存器与输入寄存器 (uint16_t)
        server->setHoldingRegisterValue(10, 1500);
        server->setInputRegisterValue(10, 220);

        // 2. 写入 32 位浮点数 (自动跨 2 个连续寄存器，支持 ABCD / CDAB 字节序)
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

    // 启动您的业务线程
    std::thread worker(businessThread, &modServer);

    // 优雅退出管理（支持 Ctrl+C）
    std::cout << "Server is running. Press Ctrl+C to exit..." << std::endl;
    // ...
    worker.join();
    modServer.stop();
    return 0;
}
```

---

## 🧪 自动化测试与压测 (Testing)

项目内嵌了 Python3 多客户端自动化并发读写与浮点数校验脚本：

```bash
# 启动服务器
./bin/modServer 1502 &

# 运行自动化压测脚本 (10 个并发客户端同时高频读写)
python3 test_alongL.py
```

**实测性能输出：**
```text
=== 测试 RDSModbusSlave (Epoll + Thread-Safe 优化版) ===
[1] 成功建立单客户端 TCP 连接
  -> 读取寄存器 #10~13 (含模拟数据与浮点数): [1566, 0, 16762, 36700]
  -> 解析寄存器 #12 浮点数 (ABCD 格式): 15.66 (符合模拟线程值预期)
[2] 启动 10 个并发客户端同时执行高频读写...
  => 10 个客户端并发执行 300 次读写全部成功！耗时: 0.159s (QPS: 1882.1 ops/s)

[ALL TESTS PASSED] 优化版 modbusServer 功能与并发验证全部通过！
```

---

## 📄 License
LGPL v2.1 or later (遵循 libmodbus 开源许可).
