#ifndef RDSMODBUSSLAVE_H
#define RDSMODBUSSLAVE_H
#include <iostream>
#include <thread>
#include <stdlib.h>
#include <iostream>
#include <mutex>
#include <string>
using namespace std;
/*如果是windows平台则要加载相应的静态库和头文件*/
#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <modbus.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "modbus.lib")
/*linux平台*/
#else
#include <modbus/modbus.h>
#include <unistd.h>
#include <error.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#endif


//#define  MAX_POINT  50000



class RDSModbusSlave
{
public:
    RDSModbusSlave(string host="0.0.0.0", uint16_t port=502);
    ~RDSModbusSlave();

public:
    void recieveMessages();
    bool modbus_set_slave_id(int id);
    bool initModbus(std::string Host_Ip, int port, bool debugging);
    
    uint8_t getTab_Input_Bits(int NumBit);
    bool setTab_Input_Bits(int NumBit, uint8_t Value);

    uint16_t getHoldingRegisterValue(int registerNumber);
    float getHoldingRegisterFloatValue(int registerStartaddress);
    
    bool setHoldingRegisterValue(int registerNumber, uint16_t Value);
    bool setHoldingRegisterValue(int registerNumber, float Value);

    bool setInputRegisterValue(int registerNumber, uint16_t Value);
    bool setInputRegisterValue(int registerNumber, float Value);
    

    

private:
    std::mutex slavemutex;
    int m_errCount{ 0 };
    int m_modbusSocket{ -1 };
    bool m_initialized{ false };
    modbus_t* ctx{ nullptr };
    modbus_mapping_t* mapping{ nullptr };
    /*Mapping*/
    int m_numBits{ 60000 };
    int m_numInputBits{ 60000 };
    int m_numRegisters{ 60000 };
    int m_numInputRegisters{ 60000 };

public:
    void loadFromConfigFile();
    void run();
};
/*annotation:
(1)https://www.jianshu.com/p/0ed380fa39eb
(2)typedef struct _modbus_mapping_t
{
    int nb_bits;                //线圈
    int start_bits;
    int nb_input_bits;          //离散输入
    int start_input_bits;
    int nb_input_registers;     //输入寄存器
    int start_input_registers;
    int nb_registers;           //保持寄存器
    int start_registers;
    uint8_t *tab_bits;
    uint8_t *tab_input_bits;
    uint16_t *tab_input_registers;
    uint16_t *tab_registers;
}modbus_mapping_t;*/
#endif // RDSMODBUSSLAVE_H

