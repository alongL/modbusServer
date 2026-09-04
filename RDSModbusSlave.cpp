#include "RDSModbusSlave.h"
#include <iostream>
#include <cstring>
#include <cerrno>

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
      m_coils(numBits, 0),
      m_discreteInputs(numInputBits, 0),
      m_holdingRegisters(numRegisters, 0),
      m_inputRegisters(numInputRegisters, 0)
{
    if (!initModbus(m_host, m_port)) {
        throw std::runtime_error("Failed to initialize native Modbus TCP server on " + host + ":" + std::to_string(port));
    }
}

RDSModbusSlave::~RDSModbusSlave() {
    stop();
}

bool RDSModbusSlave::initModbus(const std::string& hostIp, int port) {
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket < 0) {
        std::cerr << "[RDSModbusSlave] Error creating socket: " << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (hostIp == "0.0.0.0" || hostIp.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, hostIp.c_str(), &addr.sin_addr);
    }

    if (bind(m_serverSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[RDSModbusSlave] Error binding to " << hostIp << ":" << port 
                  << " - " << strerror(errno) << std::endl;
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    setNonBlocking(m_serverSocket);

    if (listen(m_serverSocket, 128) < 0) {
        std::cerr << "[RDSModbusSlave] Error listening: " << strerror(errno) << std::endl;
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

#ifndef _WIN32
    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        std::cerr << "[RDSModbusSlave] Failed to create epoll instance" << std::endl;
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    m_stopEventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_stopEventFd < 0) {
        std::cerr << "[RDSModbusSlave] Failed to create eventfd" << std::endl;
        close(m_epollFd);
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    epoll_event evStop{};
    evStop.events = EPOLLIN;
    evStop.data.fd = m_stopEventFd;
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_stopEventFd, &evStop);

    epoll_event evServer{};
    evServer.events = EPOLLIN;
    evServer.data.fd = m_serverSocket;
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_serverSocket, &evServer);
#endif

    return true;
}

void RDSModbusSlave::run() {
    if (m_running.load()) return;
    m_running.store(true);

    m_workerThread = std::thread(&RDSModbusSlave::eventLoop, this);
    std::cout << "[RDSModbusSlave] (纯原生 Epoll 模式，无第三方依赖) 运行于 " 
              << m_host << ":" << m_port << std::endl;
}

void RDSModbusSlave::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

#ifndef _WIN32
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

    if (m_serverSocket >= 0) {
        close(m_serverSocket);
        m_serverSocket = -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& [fd, session] : m_clients) {
            close(fd);
        }
        m_clients.clear();
    }

    std::cout << "[RDSModbusSlave] 服务已安全停止并释放所有资源。" << std::endl;
}

bool RDSModbusSlave::setSlaveId(int id) {
    m_slaveId = id;
    return true;
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
                break;
            }

            if (fd == m_serverSocket) {
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
        int newfd = accept(m_serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrlen);
        if (newfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }

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
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_clients[newfd] = ClientSession{newfd, {}};
        }
        m_clientCount++;

        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        std::cout << "[RDSModbusSlave] 客户端接入: " << ipBuf << ":" << ntohs(clientAddr.sin_port) 
                  << " (socket fd: " << newfd << ", 在线客户端: " << m_clientCount.load() << ")" << std::endl;
    }
}

void RDSModbusSlave::closeClient(int clientFd) {
#ifndef _WIN32
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
#endif
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients.erase(clientFd);
    }
    m_clientCount--;
    std::cout << "[RDSModbusSlave] 客户端断开: socket " << clientFd 
              << " (在线客户端: " << m_clientCount.load() << ")" << std::endl;
}

void RDSModbusSlave::handleClientData(int clientFd) {
    uint8_t buffer[1024];
    std::vector<uint8_t>* rxBufPtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clients.find(clientFd);
        if (it == m_clients.end()) return;
        rxBufPtr = &(it->second.rxBuffer);
    }

    while (true) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            closeClient(clientFd);
            return;
        } else if (n == 0) {
            closeClient(clientFd);
            return;
        }
        rxBufPtr->insert(rxBufPtr->end(), buffer, buffer + n);
    }

    // 完整的 Modbus TCP 帧解析循环 (解决粘包/拆包)
    while (rxBufPtr->size() >= 6) {
        uint16_t transId = ((*rxBufPtr)[0] << 8) | (*rxBufPtr)[1];
        uint16_t protoId = ((*rxBufPtr)[2] << 8) | (*rxBufPtr)[3];
        uint16_t length  = ((*rxBufPtr)[4] << 8) | (*rxBufPtr)[5];

        if (protoId != 0 || length < 2 || length > 260) {
            closeClient(clientFd);
            return;
        }

        size_t totalFrameSize = 6 + length;
        if (rxBufPtr->size() < totalFrameSize) {
            // 半包，等待下次数据
            return;
        }

        uint8_t unitId = (*rxBufPtr)[6];
        std::vector<uint8_t> pdu(rxBufPtr->begin() + 7, rxBufPtr->begin() + totalFrameSize);
        rxBufPtr->erase(rxBufPtr->begin(), rxBufPtr->begin() + totalFrameSize);

        // 原生 C++ 处理 Modbus PDU
        std::vector<uint8_t> respPdu = processPdu(pdu.data(), pdu.size());

        // 构造 Modbus TCP 响应帧
        uint16_t respLength = static_cast<uint16_t>(1 + respPdu.size());
        std::vector<uint8_t> resp;
        resp.reserve(6 + respLength);
        resp.push_back((transId >> 8) & 0xFF);
        resp.push_back(transId & 0xFF);
        resp.push_back(0x00);
        resp.push_back(0x00);
        resp.push_back((respLength >> 8) & 0xFF);
        resp.push_back(respLength & 0xFF);
        resp.push_back(unitId);
        resp.insert(resp.end(), respPdu.begin(), respPdu.end());

        send(clientFd, resp.data(), resp.size(), MSG_NOSIGNAL);
    }
}

// ----------------------------------------------------------------------------
// 纯原生 Modbus PDU 业务处理实现 (彻底取代 libmodbus 的 modbus_reply)
// ----------------------------------------------------------------------------
std::vector<uint8_t> RDSModbusSlave::processPdu(const uint8_t* pdu, size_t len) {
    if (len < 1) return {0x80, 0x03};
    uint8_t fc = pdu[0];

    auto makeEx = [](uint8_t f, uint8_t code) -> std::vector<uint8_t> {
        return {static_cast<uint8_t>(f | 0x80), code};
    };

    switch (fc) {
        // FC 01: Read Coils
        case 0x01: {
            if (len < 5) return makeEx(fc, 0x03);
            uint16_t start = (pdu[1] << 8) | pdu[2];
            uint16_t count = (pdu[3] << 8) | pdu[4];
            if (count < 1 || count > 2000) return makeEx(fc, 0x03);

            std::shared_lock<std::shared_mutex> lock(m_dataMutex);
            if (start + count > m_coils.size()) return makeEx(fc, 0x02);

            uint8_t byteCount = (count + 7) / 8;
            std::vector<uint8_t> resp = {fc, byteCount};
            resp.resize(2 + byteCount, 0);
            for (uint16_t i = 0; i < count; ++i) {
                if (m_coils[start + i]) {
                    resp[2 + (i / 8)] |= (1 << (i % 8));
                }
            }
            return resp;
        }

        // FC 02: Read Discrete Inputs
        case 0x02: {
            if (len < 5) return makeEx(fc, 0x03);
            uint16_t start = (pdu[1] << 8) | pdu[2];
            uint16_t count = (pdu[3] << 8) | pdu[4];
            if (count < 1 || count > 2000) return makeEx(fc, 0x03);

            std::shared_lock<std::shared_mutex> lock(m_dataMutex);
            if (start + count > m_discreteInputs.size()) return makeEx(fc, 0x02);

            uint8_t byteCount = (count + 7) / 8;
            std::vector<uint8_t> resp = {fc, byteCount};
            resp.resize(2 + byteCount, 0);
            for (uint16_t i = 0; i < count; ++i) {
                if (m_discreteInputs[start + i]) {
                    resp[2 + (i / 8)] |= (1 << (i % 8));
                }
            }
            return resp;
        }

        // FC 03: Read Holding Registers
        case 0x03: {
            if (len < 5) return makeEx(fc, 0x03);
            uint16_t start = (pdu[1] << 8) | pdu[2];
            uint16_t count = (pdu[3] << 8) | pdu[4];
            if (count < 1 || count > 125) return makeEx(fc, 0x03);

            std::shared_lock<std::shared_mutex> lock(m_dataMutex);
            if (start + count > m_holdingRegisters.size()) return makeEx(fc, 0x02);

            uint8_t byteCount = count * 2;
            std::vector<uint8_t> resp;
            resp.reserve(2 + byteCount);
            resp.push_back(fc);
            resp.push_back(byteCount);
            for (uint16_t i = 0; i < count; ++i) {
                uint16_t v = m_holdingRegisters[start + i];
                resp.push_back((v >> 8) & 0xFF);
                resp.push_back(v & 0xFF);
            }
            return resp;
        }

        // FC 04: Read Input Registers
        case 0x04: {
            if (len < 5) return makeEx(fc, 0x03);
            uint16_t start = (pdu[1] << 8) | pdu[2];
            uint16_t count = (pdu[3] << 8) | pdu[4];
            if (count < 1 || count > 125) return makeEx(fc, 0x03);

            std::shared_lock<std::shared_mutex> lock(m_dataMutex);
            if (start + count > m_inputRegisters.size()) return makeEx(fc, 0x02);

            uint8_t byteCount = count * 2;
            std::vector<uint8_t> resp;
            resp.reserve(2 + byteCount);
            resp.push_back(fc);
            resp.push_back(byteCount);
            for (uint16_t i = 0; i < count; ++i) {
                uint16_t v = m_inputRegisters[start + i];
                resp.push_back((v >> 8) & 0xFF);
                resp.push_back(v & 0xFF);
            }
            return resp;
        }

        // FC 05: Write Single Coil
        case 0x05: {
            if (len < 5) return makeEx(fc, 0x03);
            uint16_t addr = (pdu[1] << 8) | pdu[2];
            uint16_t val  = (pdu[3] << 8) | pdu[4];
            if (val != 0xFF00 && val != 0x0000) return makeEx(fc, 0x03);

            std::unique_lock<std::shared_mutex> lock(m_dataMutex);
            if (addr >= m_coils.size()) return makeEx(fc, 0x02);
            m_coils[addr] = (val == 0xFF00) ? 1 : 0;
            return std::vector<uint8_t>(pdu, pdu + 5);
        }

        // FC 06: Write Single Register
        case 0x06: {
            if (len < 5) return makeEx(fc, 0x03);
            uint16_t addr = (pdu[1] << 8) | pdu[2];
            uint16_t val  = (pdu[3] << 8) | pdu[4];

            std::unique_lock<std::shared_mutex> lock(m_dataMutex);
            if (addr >= m_holdingRegisters.size()) return makeEx(fc, 0x02);
            m_holdingRegisters[addr] = val;
            return std::vector<uint8_t>(pdu, pdu + 5);
        }

        // FC 15 (0x0F): Write Multiple Coils
        case 0x0F: {
            if (len < 6) return makeEx(fc, 0x03);
            uint16_t start = (pdu[1] << 8) | pdu[2];
            uint16_t count = (pdu[3] << 8) | pdu[4];
            uint8_t byteCount = pdu[5];
            if (count < 1 || count > 1968) return makeEx(fc, 0x03);
            if (byteCount != (count + 7) / 8 || len < static_cast<size_t>(6 + byteCount)) return makeEx(fc, 0x03);

            std::unique_lock<std::shared_mutex> lock(m_dataMutex);
            if (start + count > m_coils.size()) return makeEx(fc, 0x02);
            for (uint16_t i = 0; i < count; ++i) {
                uint8_t b = pdu[6 + (i / 8)];
                m_coils[start + i] = (b & (1 << (i % 8))) ? 1 : 0;
            }
            return std::vector<uint8_t>(pdu, pdu + 5);
        }

        // FC 16 (0x10): Write Multiple Registers
        case 0x10: {
            if (len < 6) return makeEx(fc, 0x03);
            uint16_t start = (pdu[1] << 8) | pdu[2];
            uint16_t count = (pdu[3] << 8) | pdu[4];
            uint8_t byteCount = pdu[5];
            if (count < 1 || count > 123) return makeEx(fc, 0x03);
            if (byteCount != count * 2 || len < static_cast<size_t>(6 + byteCount)) return makeEx(fc, 0x03);

            std::unique_lock<std::shared_mutex> lock(m_dataMutex);
            if (start + count > m_holdingRegisters.size()) return makeEx(fc, 0x02);
            for (uint16_t i = 0; i < count; ++i) {
                uint16_t val = (pdu[6 + i * 2] << 8) | pdu[7 + i * 2];
                m_holdingRegisters[start + i] = val;
            }
            return std::vector<uint8_t>(pdu, pdu + 5);
        }

        default:
            return makeEx(fc, 0x01);
    }
}

// ----------------------------------------------------------------------------
// 线程安全点位 API
// ----------------------------------------------------------------------------
uint8_t RDSModbusSlave::getTab_Input_Bits(int numBit) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (numBit < 0 || static_cast<size_t>(numBit) >= m_discreteInputs.size()) return 0;
    return m_discreteInputs[numBit];
}

bool RDSModbusSlave::setTab_Input_Bits(int numBit, uint8_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (numBit < 0 || static_cast<size_t>(numBit) >= m_discreteInputs.size()) return false;
    m_discreteInputs[numBit] = value ? 1 : 0;
    return true;
}

uint8_t RDSModbusSlave::getCoil(int numBit) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (numBit < 0 || static_cast<size_t>(numBit) >= m_coils.size()) return 0;
    return m_coils[numBit];
}

bool RDSModbusSlave::setCoil(int numBit, uint8_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (numBit < 0 || static_cast<size_t>(numBit) >= m_coils.size()) return false;
    m_coils[numBit] = value ? 1 : 0;
    return true;
}

uint16_t RDSModbusSlave::getHoldingRegisterValue(int registerNumber) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (registerNumber < 0 || static_cast<size_t>(registerNumber) >= m_holdingRegisters.size()) return 0;
    return m_holdingRegisters[registerNumber];
}

bool RDSModbusSlave::setHoldingRegisterValue(int registerNumber, uint16_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (registerNumber < 0 || static_cast<size_t>(registerNumber) >= m_holdingRegisters.size()) return false;
    m_holdingRegisters[registerNumber] = value;
    return true;
}

uint16_t RDSModbusSlave::getInputRegisterValue(int registerNumber) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (registerNumber < 0 || static_cast<size_t>(registerNumber) >= m_inputRegisters.size()) return 0;
    return m_inputRegisters[registerNumber];
}

bool RDSModbusSlave::setInputRegisterValue(int registerNumber, uint16_t value) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (registerNumber < 0 || static_cast<size_t>(registerNumber) >= m_inputRegisters.size()) return false;
    m_inputRegisters[registerNumber] = value;
    return true;
}

// ----------------------------------------------------------------------------
// 浮点数存取实现 (纯原生内存转换，彻底不依赖 libmodbus)
// ----------------------------------------------------------------------------
static void encodeFloat(float val, uint16_t& r0, uint16_t& r1, FloatEndian endian) {
    uint32_t u = 0;
    std::memcpy(&u, &val, sizeof(float));
    uint8_t a = (u >> 24) & 0xFF;
    uint8_t b = (u >> 16) & 0xFF;
    uint8_t c = (u >> 8) & 0xFF;
    uint8_t d = u & 0xFF;

    if (endian == FloatEndian::ABCD) {
        r0 = (a << 8) | b;
        r1 = (c << 8) | d;
    } else if (endian == FloatEndian::CDAB) {
        r0 = (c << 8) | d;
        r1 = (a << 8) | b;
    } else if (endian == FloatEndian::BADC) {
        r0 = (b << 8) | a;
        r1 = (d << 8) | c;
    } else { // DCBA
        r0 = (d << 8) | c;
        r1 = (b << 8) | a;
    }
}

static float decodeFloat(uint16_t r0, uint16_t r1, FloatEndian endian) {
    uint8_t a = 0, b = 0, c = 0, d = 0;
    if (endian == FloatEndian::ABCD) {
        a = (r0 >> 8) & 0xFF; b = r0 & 0xFF;
        c = (r1 >> 8) & 0xFF; d = r1 & 0xFF;
    } else if (endian == FloatEndian::CDAB) {
        c = (r0 >> 8) & 0xFF; d = r0 & 0xFF;
        a = (r1 >> 8) & 0xFF; b = r1 & 0xFF;
    } else if (endian == FloatEndian::BADC) {
        b = (r0 >> 8) & 0xFF; a = r0 & 0xFF;
        d = (r1 >> 8) & 0xFF; c = r1 & 0xFF;
    } else { // DCBA
        d = (r0 >> 8) & 0xFF; c = r0 & 0xFF;
        b = (r1 >> 8) & 0xFF; a = r1 & 0xFF;
    }
    uint32_t u = (static_cast<uint32_t>(a) << 24) |
                 (static_cast<uint32_t>(b) << 16) |
                 (static_cast<uint32_t>(c) << 8)  |
                 static_cast<uint32_t>(d);
    float val = 0.0f;
    std::memcpy(&val, &u, sizeof(float));
    return val;
}

bool RDSModbusSlave::setHoldingRegisterValue(int addr, float value, FloatEndian endian) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (addr < 0 || static_cast<size_t>(addr) >= (m_holdingRegisters.size() - 1)) return false;
    encodeFloat(value, m_holdingRegisters[addr], m_holdingRegisters[addr + 1], endian);
    return true;
}

float RDSModbusSlave::getHoldingRegisterFloatValue(int addr, FloatEndian endian) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (addr < 0 || static_cast<size_t>(addr) >= (m_holdingRegisters.size() - 1)) return 0.0f;
    return decodeFloat(m_holdingRegisters[addr], m_holdingRegisters[addr + 1], endian);
}

bool RDSModbusSlave::setInputRegisterValue(int addr, float value, FloatEndian endian) {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    if (addr < 0 || static_cast<size_t>(addr) >= (m_inputRegisters.size() - 1)) return false;
    encodeFloat(value, m_inputRegisters[addr], m_inputRegisters[addr + 1], endian);
    return true;
}

float RDSModbusSlave::getInputRegisterFloatValue(int addr, FloatEndian endian) const {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    if (addr < 0 || static_cast<size_t>(addr) >= (m_inputRegisters.size() - 1)) return 0.0f;
    return decodeFloat(m_inputRegisters[addr], m_inputRegisters[addr + 1], endian);
}
