#ifndef RDSMODBUSSLAVE_H
#define RDSMODBUSSLAVE_H

#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
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
 * 彻底移除 libmodbus 依赖的纯原生现代 C++ Modbus TCP 服务器 (RDSModbusSlave)
 * 
 * 特性：
 * 1. 100% 纯原生 C++ 实现，零外部第三方依赖 (完全不需要 libmodbus)；
 * 2. 基于 Linux 原生 epoll Reactor 高性能非阻塞事件驱动；
 * 3. 完整支持 Modbus TCP 核心功能码：
 *    - FC 01: Read Coils
 *    - FC 02: Read Discrete Inputs
 *    - FC 03: Read Holding Registers
 *    - FC 04: Read Input Registers
 *    - FC 05: Write Single Coil
 *    - FC 06: Write Single Register
 *    - FC 15 (0x0F): Write Multiple Coils
 *    - FC 16 (0x10): Write Multiple Registers
 * 4. 线程安全：底层数据区由 std::shared_mutex 读写锁全生命周期保护；
 * 5. 优雅退出：基于 Linux eventfd 实现毫秒级平滑停机与 RAII 资源回收。
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

    RDSModbusSlave(const RDSModbusSlave&) = delete;
    RDSModbusSlave& operator=(const RDSModbusSlave&) = delete;

    bool initModbus(const std::string& hostIp, int port);
    void run();
    void stop();

    bool setSlaveId(int id);

    // 线程安全的点位读写接口 (保持与原接口兼容)
    uint8_t getTab_Input_Bits(int numBit) const;
    bool setTab_Input_Bits(int numBit, uint8_t value);

    uint8_t getCoil(int numBit) const;
    bool setCoil(int numBit, uint8_t value);

    uint16_t getHoldingRegisterValue(int registerNumber) const;
    bool setHoldingRegisterValue(int registerNumber, uint16_t value);

    uint16_t getInputRegisterValue(int registerNumber) const;
    bool setInputRegisterValue(int registerNumber, uint16_t value);

    // 浮点数存取接口 (支持标准 ABCD 和 CDAB 模式)
    float getHoldingRegisterFloatValue(int registerStartaddress, FloatEndian endian = FloatEndian::ABCD) const;
    bool setHoldingRegisterValue(int registerStartaddress, float value, FloatEndian endian = FloatEndian::ABCD);

    float getInputRegisterFloatValue(int registerStartaddress, FloatEndian endian = FloatEndian::ABCD) const;
    bool setInputRegisterValue(int registerStartaddress, float value, FloatEndian endian = FloatEndian::ABCD);

    bool isRunning() const { return m_running.load(); }
    int getActiveClientCount() const { return m_clientCount.load(); }

private:
    struct ClientSession {
        int fd;
        std::vector<uint8_t> rxBuffer;
    };

    void eventLoop();
    void handleNewConnection();
    void handleClientData(int clientFd);
    void closeClient(int clientFd);
    std::vector<uint8_t> processPdu(const uint8_t* pdu, size_t len);

    static bool setNonBlocking(int fd);

    std::string m_host{"0.0.0.0"};
    uint16_t m_port{502};
    int m_slaveId{1};

    int m_serverSocket{-1};
    int m_epollFd{-1};
    int m_stopEventFd{-1};

    // 纯原生 C++ 内存数据区 (彻底取代 libmodbus 的 modbus_mapping_t)
    mutable std::shared_mutex m_dataMutex;
    std::vector<uint8_t>  m_coils;            // 线圈 (0x)
    std::vector<uint8_t>  m_discreteInputs;   // 离散输入 (1x)
    std::vector<uint16_t> m_holdingRegisters; // 保持寄存器 (4x)
    std::vector<uint16_t> m_inputRegisters;   // 输入寄存器 (3x)

    std::mutex m_clientsMutex;
    std::unordered_map<int, ClientSession> m_clients;

    std::atomic<bool> m_running{false};
    std::atomic<int> m_clientCount{0};
    std::thread m_workerThread;
};

#endif // RDSMODBUSSLAVE_H
