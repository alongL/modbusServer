#ifndef RDSMODBUSSLAVE_H
#define RDSMODBUSSLAVE_H

#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <modbus.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "modbus.lib")
#else
#include <modbus/modbus.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#endif

enum class FloatEndian {
    ABCD, // 标准大端 (Motorola / 西门子)
    CDAB, // 字交换小端 (Mid-Endian / 汇川 / 台达)
    BADC, // 字节交换
    DCBA  // 完全小端 (Intel)
};

/**
 * 优化后的工业级 RDSModbusSlave
 * 
 * 核心优化：
 * 1. 彻底淘汰有 1024 fd 限制的 select()，改用 Linux 原生高性能 epoll Reactor；
 * 2. 彻底解决多线程数据竞争（Data Race）：在外部修改与 modbus_reply 之间加入全局读写锁；
 * 3. 彻底解决析构 Use-After-Free 崩溃：消除 detach，采用原子退出与 join() 优雅停机；
 * 4. 修复浮点数存取大小端不一致 Bug；
 * 5. 客户端套接字设置为非阻塞，防止慢连接/恶意半包导致服务器单线程卡死。
 */
class RDSModbusSlave {
public:
    explicit RDSModbusSlave(const std::string& host = "0.0.0.0", 
                            uint16_t port = 502, 
                            int numBits = 10000, 
                            int numInputBits = 10000, 
                            int numRegisters = 10000, 
                            int numInputRegisters = 10000);
    ~RDSModbusSlave();

    // 禁用拷贝以确保资源唯一性
    RDSModbusSlave(const RDSModbusSlave&) = delete;
    RDSModbusSlave& operator=(const RDSModbusSlave&) = delete;

    bool initModbus(const std::string& hostIp, int port, bool debugging = false);
    void run();
    void stop();

    bool setSlaveId(int id);

    // 线程安全的点位读写接口
    uint8_t getTab_Input_Bits(int numBit) const;
    bool setTab_Input_Bits(int numBit, uint8_t value);

    uint8_t getCoil(int numBit) const;
    bool setCoil(int numBit, uint8_t value);

    uint16_t getHoldingRegisterValue(int registerNumber) const;
    bool setHoldingRegisterValue(int registerNumber, uint16_t value);

    uint16_t getInputRegisterValue(int registerNumber) const;
    bool setInputRegisterValue(int registerNumber, uint16_t value);

    // 浮点数存取接口（支持标准 ABCD 和 CDAB 模式）
    float getHoldingRegisterFloatValue(int registerStartaddress, FloatEndian endian = FloatEndian::ABCD) const;
    bool setHoldingRegisterValue(int registerStartaddress, float value, FloatEndian endian = FloatEndian::ABCD);

    float getInputRegisterFloatValue(int registerStartaddress, FloatEndian endian = FloatEndian::ABCD) const;
    bool setInputRegisterValue(int registerStartaddress, float value, FloatEndian endian = FloatEndian::ABCD);

    bool isRunning() const { return m_running.load(); }
    int getActiveClientCount() const { return m_clientCount.load(); }

private:
    void eventLoop();
    void handleNewConnection();
    void handleClientData(int clientFd);
    void closeClient(int clientFd);
    static bool setNonBlocking(int fd);

    std::string m_host{"0.0.0.0"};
    uint16_t m_port{502};
    int m_modbusSocket{-1};
    int m_epollFd{-1};
    int m_stopEventFd{-1};

    modbus_t* m_ctx{nullptr};
    modbus_mapping_t* m_mapping{nullptr};

    // 线程安全：使用读写锁保护底层 mapping 结构体
    mutable std::shared_mutex m_dataMutex;

    int m_numBits{10000};
    int m_numInputBits{10000};
    int m_numRegisters{10000};
    int m_numInputRegisters{10000};

    std::atomic<bool> m_running{false};
    std::atomic<int> m_clientCount{0};
    std::thread m_workerThread;
};

#endif // RDSMODBUSSLAVE_H
