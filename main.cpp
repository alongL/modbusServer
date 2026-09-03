#include <iostream>
#include "RDSModbusSlave.h"
#include <thread>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <ctime>

std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    (void)sig;
    g_running.store(false);
}

void simulationWorker(RDSModbusSlave* server) {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    while (g_running.load()) {
        uint16_t value = 1000 + (std::rand() % 1000);
        float valuef = value / 100.0f;

        server->setInputRegisterValue(10, value);
        server->setHoldingRegisterValue(10, value);

        // 浮点数写入（测试标准 ABCD 模式）
        server->setInputRegisterValue(12, valuef, FloatEndian::ABCD);
        server->setHoldingRegisterValue(12, valuef, FloatEndian::ABCD);

        server->setTab_Input_Bits(1, 1);
        server->setCoil(1, 1);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int port = 1502;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   RDSModbusSlave (Epoll + Thread-Safe 优化版)   " << std::endl;
    std::cout << "=================================================" << std::endl;

    try {
        RDSModbusSlave modServer("0.0.0.0", port);
        modServer.run();

        // 启动后台模拟点位数据刷新线程
        std::thread simThread(simulationWorker, &modServer);

        std::cout << ">> 服务器就绪，监听端口: " << port << std::endl;
        std::cout << ">> 按 Ctrl+C 优雅退出..." << std::endl;

        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "\n[System] 正在停止服务..." << std::endl;
        if (simThread.joinable()) {
            simThread.join();
        }
        modServer.stop();
    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[System] 服务器已安全退出。" << std::endl;
    return 0;
}
