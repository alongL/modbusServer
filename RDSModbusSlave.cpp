#include "RDSModbusSlave.h"
#include <cstring>
#include <cerrno>
#include <iostream>

#ifdef _WIN32
typedef int socklen_t;
#endif

bool RDSModbusSlave::setNonBlocking(int fd) {
#ifndef _WIN32
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
#else
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#endif
}

RDSModbusSlave::RDSModbusSlave(const std::string& host, 
                               uint16_t port, 
                               int numBits, 
                               int numInputBits, 
                               int numRegisters, 
                               int numInputRegisters)
    : m_host(host), m_port(port),
      m_numBits(numBits), m_numInputBits(numInputBits),
      m_numRegisters(numRegisters), m_numInputRegisters(numInputRegisters)
{
    if (!initModbus(m_host, m_port, false)) {
        throw std::runtime_error("Failed to initialize Modbus TCP server on " + host + ":" + std::to_string(port));
    }
}

RDSModbusSlave::~RDSModbusSlave() {
    stop();
}

bool RDSModbusSlave::initModbus(const std::string& hostIp, int port, bool debugging) {
    m_ctx = modbus_new_tcp(hostIp.c_str(), port);
    if (m_ctx == nullptr) {
        std::cerr << "[RDSModbusSlave] Error creating modbus tcp context: " << modbus_strerror(errno) << std::endl;
        return false;
    }
    modbus_set_debug(m_ctx, debugging ? 1 : 0);

    // 增加连接队列容量（原为 1，现优化为 128 防止瞬时突发连接被拒）
    m_modbusSocket = modbus_tcp_listen(m_ctx, 128);
    if (m_modbusSocket < 0) {
        std::cerr << "[RDSModbusSlave] Error listening on " << hostIp << ":" << port << ": " << modbus_strerror(errno) << std::endl;
        modbus_free(m_ctx);
        m_ctx = nullptr;
        return false;
    }

    // 设置地址与端口复用
    int opt = 1;
    setsockopt(m_modbusSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(m_modbusSocket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    // 设置监听 Socket 为非阻塞
    setNonBlocking(m_modbusSocket);

    // 初始化点位数据区
    m_mapping = modbus_mapping_new(m_numBits, m_numInputBits, m_numInputRegisters, m_numRegisters);
    if (m_mapping == nullptr) {
        std::cerr << "[RDSModbusSlave] Unable to allocate mapping: " << modbus_strerror(errno) << std::endl;
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
        return false;
    }

#ifndef _WIN32
    // 初始化 Linux 原生 epoll
    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        std::cerr << "[RDSModbusSlave] Failed to create epoll instance: " << strerror(errno) << std::endl;
        return false;
    }

    // 创建 eventfd 用于优雅退出通知
    m_stopEventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_stopEventFd < 0) {
        std::cerr << "[RDSModbusSlave] Failed to create eventfd: " << strerror(errno) << std::endl;
        return false;
    }

    epoll_event evStop{};
    evStop.events = EPOLLIN;
    evStop.data.fd = m_stopEventFd;
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_stopEventFd, &evStop);

    epoll_event evServer{};
    evServer.events = EPOLLIN;
    evServer.data.fd = m_modbusSocket;
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_modbusSocket, &evServer);
#endif

    return true;
}

void RDSModbusSlave::run() {
    if (m_running.load()) return;
    m_running.store(true);

    m_workerThread = std::thread(&RDSModbusSlave::eventLoop, this);
    std::cout << "[RDSModbusSlave] (优化版 Epoll 反应堆模式) 运行于 " 
              << m_host << ":" << m_port << std::endl;
}

void RDSModbusSlave::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

#ifndef _WIN32
    // 唤醒 epoll_wait 退出
    if (m_stopEventFd >= 0) {
        uint64_t val = 1;
        ssize_t ret = write(m_stopEventFd, &val, sizeof(val));
        (void)ret;
    }
#endif

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

#ifndef _WIN32
    if (m_stopEventFd >= 0) {
        close(m_stopEventFd);
        m_stopEventFd = -1;
    }
    if (m_epollFd >= 0) {
        close(m_epollFd);
        m_epollFd = -1;
    }
#endif

    if (m_modbusSocket >= 0) {
#ifdef _WIN32
        closesocket(m_modbusSocket);
#else
        close(m_modbusSocket);
#endif
        m_modbusSocket = -1;
    }

    if (m_mapping != nullptr) {
        std::unique_lock<std::shared_mutex> lock(m_dataMutex);
        modbus_mapping_free(m_mapping);
        m_mapping = nullptr;
    }

    if (m_ctx != nullptr) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }

    std::cout << "[RDSModbusSlave] 服务已安全停止并释放资源。" << std::endl;
}

bool RDSModbusSlave::setSlaveId(int id) {
    if (m_ctx == nullptr) return false;
    return modbus_set_slave(m_ctx, id) != -1;
}

void RDSModbusSlave::eventLoop() {
#ifndef _WIN32
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (m_running.load()) {
        int nfds = epoll_wait(m_epollFd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == m_stopEventFd) {
                // 收到退出通知
                break;
            }

            if (fd == m_modbusSocket) {
                handleNewConnection();
                continue;
            }

            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                closeClient(fd);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                handleClientData(fd);
            }
        }
    }
#endif
}

void RDSModbusSlave::handleNewConnection() {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t addrlen = sizeof(clientAddr);
        int newfd = accept(m_modbusSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrlen);
        if (newfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }

        // 关键优化：设置客户端套接字为非阻塞，防止慢连接挂死服务器
        setNonBlocking(newfd);

#ifndef _WIN32
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = newfd;
        if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, newfd, &ev) < 0) {
            close(newfd);
            continue;
        }
#endif
        m_clientCount++;
        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        std::cout << "[RDSModbusSlave] 客户端接入: " << ipBuf << ":" << ntohs(clientAddr.sin_port) 
                  << " (socket fd: " << newfd << ", 当前在线: " << m_clientCount.load() << ")" << std::endl;
    }
}

void RDSModbusSlave::handleClientData(int clientFd) {
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

    modbus_set_socket(m_ctx, clientFd);
    int rc = modbus_receive(m_ctx, query);
    if (rc > 0) {
        // 关键修复：加写锁保护 mapping，防止与外部业务线程发生 Data Race！
        std::unique_lock<std::shared_mutex> lock(m_dataMutex);
        modbus_reply(m_ctx, query, rc, m_mapping);
    } else if (rc == -1) {
        // 客户端断开连接或传输错误
        closeClient(clientFd);
    }
}

void RDSModbusSlave::closeClient(int clientFd) {
#ifndef _WIN32
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
#else
    closesocket(clientFd);
#endif
    m_clientCount--;
    std::cout << "[RDSModbusSlave] 客户端连接断开: socket " << clientFd 
              << " (当前在线: " << m_clientCount.load() << ")" << std::endl;
}

// ----------------------------------------------------------------------------
// 线程安全点位读写实现
// ----------------------------------------------------------------------------

uint8_t RDSModbusSlave::getTab_Input_Bits(int numBit) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || numBit < 0 || numBit >= m_numInputBits) return 0;
    return m_mapping->tab_input_bits[numBit];
}

bool RDSModbusSlave::setTab_Input_Bits(int numBit, uint8_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || numBit < 0 || numBit >= m_numInputBits) return false;
    m_mapping->tab_input_bits[numBit] = value ? 1 : 0;
    return true;
}

uint8_t RDSModbusSlave::getCoil(int numBit) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || numBit < 0 || numBit >= m_numBits) return 0;
    return m_mapping->tab_bits[numBit];
}

bool RDSModbusSlave::setCoil(int numBit, uint8_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || numBit < 0 || numBit >= m_numBits) return false;
    m_mapping->tab_bits[numBit] = value ? 1 : 0;
    return true;
}

uint16_t RDSModbusSlave::getHoldingRegisterValue(int registerNumber) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerNumber < 0 || registerNumber >= m_numRegisters) return 0;
    return m_mapping->tab_registers[registerNumber];
}

bool RDSModbusSlave::setHoldingRegisterValue(int registerNumber, uint16_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerNumber < 0 || registerNumber >= m_numRegisters) return false;
    m_mapping->tab_registers[registerNumber] = value;
    return true;
}

uint16_t RDSModbusSlave::getInputRegisterValue(int registerNumber) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerNumber < 0 || registerNumber >= m_numInputRegisters) return 0;
    return m_mapping->tab_input_registers[registerNumber];
}

bool RDSModbusSlave::setInputRegisterValue(int registerNumber, uint16_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerNumber < 0 || registerNumber >= m_numInputRegisters) return false;
    m_mapping->tab_input_registers[registerNumber] = value;
    return true;
}

// ----------------------------------------------------------------------------
// 浮点数存取实现（彻底解决 ABCD / CDAB 字节序对称性）
// ----------------------------------------------------------------------------

bool RDSModbusSlave::setHoldingRegisterValue(int registerStartaddress, float value, FloatEndian endian) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerStartaddress < 0 || registerStartaddress >= (m_numRegisters - 1)) return false;

    if (endian == FloatEndian::ABCD) {
        modbus_set_float_abcd(value, &m_mapping->tab_registers[registerStartaddress]);
    } else if (endian == FloatEndian::CDAB) {
        modbus_set_float_cdab(value, &m_mapping->tab_registers[registerStartaddress]);
    } else if (endian == FloatEndian::BADC) {
        modbus_set_float_badc(value, &m_mapping->tab_registers[registerStartaddress]);
    } else {
        modbus_set_float_dcba(value, &m_mapping->tab_registers[registerStartaddress]);
    }
    return true;
}

float RDSModbusSlave::getHoldingRegisterFloatValue(int registerStartaddress, FloatEndian endian) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerStartaddress < 0 || registerStartaddress >= (m_numRegisters - 1)) return 0.0f;

    if (endian == FloatEndian::ABCD) {
        return modbus_get_float_abcd(&m_mapping->tab_registers[registerStartaddress]);
    } else if (endian == FloatEndian::CDAB) {
        return modbus_get_float_cdab(&m_mapping->tab_registers[registerStartaddress]);
    } else if (endian == FloatEndian::BADC) {
        return modbus_get_float_badc(&m_mapping->tab_registers[registerStartaddress]);
    } else {
        return modbus_get_float_dcba(&m_mapping->tab_registers[registerStartaddress]);
    }
}

bool RDSModbusSlave::setInputRegisterValue(int registerStartaddress, float value, FloatEndian endian) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerStartaddress < 0 || registerStartaddress >= (m_numInputRegisters - 1)) return false;

    if (endian == FloatEndian::ABCD) {
        modbus_set_float_abcd(value, &m_mapping->tab_input_registers[registerStartaddress]);
    } else if (endian == FloatEndian::CDAB) {
        modbus_set_float_cdab(value, &m_mapping->tab_input_registers[registerStartaddress]);
    } else if (endian == FloatEndian::BADC) {
        modbus_set_float_badc(value, &m_mapping->tab_input_registers[registerStartaddress]);
    } else {
        modbus_set_float_dcba(value, &m_mapping->tab_input_registers[registerStartaddress]);
    }
    return true;
}

float RDSModbusSlave::getInputRegisterFloatValue(int registerStartaddress, FloatEndian endian) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (!m_mapping || registerStartaddress < 0 || registerStartaddress >= (m_numInputRegisters - 1)) return 0.0f;

    if (endian == FloatEndian::ABCD) {
        return modbus_get_float_abcd(&m_mapping->tab_input_registers[registerStartaddress]);
    } else if (endian == FloatEndian::CDAB) {
        return modbus_get_float_cdab(&m_mapping->tab_input_registers[registerStartaddress]);
    } else if (endian == FloatEndian::BADC) {
        return modbus_get_float_badc(&m_mapping->tab_input_registers[registerStartaddress]);
    } else {
        return modbus_get_float_dcba(&m_mapping->tab_input_registers[registerStartaddress]);
    }
}
