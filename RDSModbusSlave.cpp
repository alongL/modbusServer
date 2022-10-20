#include "RDSModbusSlave.h"
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      modbus initialization
 * @param      IP/PORT/debugflag
 * @version    v1
 * @return     null
 * @date       2021/10/6
 **************************************************************/
bool RDSModbusSlave::initModbus(std::string Host_Ip = "127.0.0.1", int port = 502, bool debugging = true)
{
    ctx = modbus_new_tcp(Host_Ip.c_str(), port);
    modbus_set_debug(ctx, debugging);
    if (ctx == NULL)
    {
        fprintf(stderr, "There was an error allocating the modbus\n");
        throw - 1;
    }
    m_modbusSocket = modbus_tcp_listen(ctx, 1);
    /*设置线圈, 离散输入, 输入寄存器, 保持寄存器个数(数组元素个数))*/
    mapping = modbus_mapping_new(m_numBits, m_numInputBits, m_numInputRegisters, m_numRegisters);
    if (mapping == NULL)
    {
        fprintf(stderr, "Unable to assign mapping：%s\n", modbus_strerror(errno));
        modbus_free(ctx);
        m_initialized = false;
        return false;
    }
    m_initialized = true;
    return true;
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      Constructor
 * @version    v1
 * @return     null
 * @date       2021/10/6
 **************************************************************/
RDSModbusSlave::RDSModbusSlave(uint16_t port)
{
    initModbus("0.0.0.0", port, false);
    //TODO：
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      Destructor
 * @version    v1
 * @return     null
 * @date       2021/10/6
 **************************************************************/
RDSModbusSlave::~RDSModbusSlave()
{
    modbus_mapping_free(mapping);
    modbus_close(ctx);
    modbus_free(ctx);
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      loadFromConfigFile
 * @version    v1
 * @return     null
 * @date       2021/10/18
 **************************************************************/
void RDSModbusSlave::loadFromConfigFile()
{
    return;
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      run
 * @version    v1
 * @return     null
 * @date       2021/10/18
 **************************************************************/
void RDSModbusSlave::run()
{
    std::thread loop([this]()
    {
        while (true)
        {
            if (m_initialized)
            {
                recieveMessages();
            }
            else
            {
                m_initialized = true;
            }
        }
    });
    loop.detach();
    return;
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      modbus_set_slave_id
 * @param      id
 * @version    v1
 * @return     null
 * @date       2021/10/19
 **************************************************************/
bool RDSModbusSlave::modbus_set_slave_id(int id)
{
    int rc = modbus_set_slave(ctx, id);
    if (rc == -1)
    {
        fprintf(stderr, "Invalid slave id\n");
        modbus_free(ctx);
        return false;
    }
    return true;
}


bool RDSModbusSlave::setInputRegisterValue(int registerStartaddress, uint16_t Value)
{
    if (registerStartaddress > (m_numRegisters - 1))
    {
        return false;
    }
    slavemutex.lock();
    mapping->tab_input_registers[registerStartaddress] = Value;
    slavemutex.unlock();
    return true;
}





/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      setRegisterValue(设置保存寄存器的值，类型为uint16_t)
 * @param      registerStartaddress(保存寄存器的起始地址)
 * @param      Value(写入到保存寄存器里的值)
 * @version    v1
 * @return     null
 * @date       2021/10/6
 **************************************************************/
bool RDSModbusSlave::setRegisterValue(int registerStartaddress, uint16_t Value)
{
    if (registerStartaddress > (m_numRegisters - 1))
    {
        return false;
    }
    slavemutex.lock();
    mapping->tab_registers[registerStartaddress] = Value;
    slavemutex.unlock();
    return true;
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      getRegisterValue(获取保存寄存器的值)
 * @param      registerStartaddress(保存寄存器的起始地址)
 * @version    v1
 * @return     null
 * @date       2021/10/6
 **************************************************************/
uint16_t RDSModbusSlave::getRegisterValue(int registerStartaddress)
{
    if (!m_initialized)
    {
        return -1;
    }
    return mapping->tab_registers[registerStartaddress];
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      setTab_Input_Bits(设置输入寄存器某一位的值)
 * @param      NumBit(输入寄存器的起始地址)
 * @param      Value(输入寄存器的值)
 * @version    v1
 * @return     null
 * @date       2021/10/8
 **************************************************************/
bool RDSModbusSlave::setTab_Input_Bits(int NumBit, uint8_t Value)
{
    if (NumBit > (m_numInputBits - 1))
    {
        return false;
    }
    slavemutex.lock();
    mapping->tab_input_bits[NumBit] = Value;
    slavemutex.unlock();
    return true;
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      getTab_Input_Bits(获取输入寄存器某一位的值)
 * @param      NumBit(输入寄存器相应的bit位)
 * @version    v1
 * @return     null
 * @date       2021/10/8
 **************************************************************/
uint8_t RDSModbusSlave::getTab_Input_Bits(int NumBit)
{
    if (!m_initialized)
    {
        return -1;
    }
    return mapping->tab_input_bits[NumBit];
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      setRegisterFloatValue(设置浮点值)
 * @param      (Value：浮点值，registerStartaddress寄存器起始地址)
 * @version    v1
 * @return     null
 * @date       2021/10/8
 **************************************************************/
bool RDSModbusSlave::setRegisterFloatValue(int registerStartaddress, float Value)
{
    if (registerStartaddress > (m_numRegisters - 2))
    {
        return false;
    }
    /*小端模式*/
    slavemutex.lock();
    modbus_set_float(Value, &mapping->tab_registers[registerStartaddress]);
    slavemutex.unlock();
    return true;
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      获取寄存器里的浮点数 
 * @param      registerStartaddress寄存器起始地址
 * @version    v1
 * @return     两个uint16_t拼接而成的浮点值
 * @date       2021/10/6
 **************************************************************/
float RDSModbusSlave::getRegisterFloatValue(int registerStartaddress)
{
    if (!m_initialized)
    {
        return -1.0f;
    }
    return modbus_get_float_badc(&mapping->tab_registers[registerStartaddress]);
}
/***************************************************************
 * @file       RDSModbusSlave.cpp
 * @author     seer-txj
 * @brief      支持多个master同时连接
 * @version    v1
 * @return     null
 * @date       2021/10/6
 **************************************************************/
void RDSModbusSlave::recieveMessages()
{
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
    int fd_num = 0, fd_max = 0, ret = 0, i = 0, clnt_sock = -1;
    fd_set reads, cpy_reads;
    FD_ZERO(&reads);
    FD_SET(m_modbusSocket, &reads);
    fd_max = m_modbusSocket;
    while (true)
    {
        cpy_reads = reads;
        if ((fd_num = select(fd_max + 1, &cpy_reads, 0, 0, 0)) == -1)
            break;
        if (fd_num == 0)
            continue;
        for (i = 0; i < fd_max + 1; i++)
        {
            if (FD_ISSET(i, &cpy_reads))
            {
                if (i == m_modbusSocket)
                {
                    clnt_sock = modbus_tcp_accept(ctx, &m_modbusSocket);
                    if ((m_modbusSocket == -1) || (clnt_sock == -1))
                    {
                        std::cerr << modbus_strerror(errno) << std::endl;
                        continue;
                    }
                    FD_SET(clnt_sock, &reads);
                    if (fd_max < clnt_sock)
                        fd_max = clnt_sock;
                }
                else
                {
                    ret = modbus_receive(ctx, query);
                    if (ret == 0)
                    {
                        m_errCount = 0;
                        continue;
                    }
                    else if (ret > 0)
                    {
                        m_errCount = 0;
                        modbus_reply(ctx, query, sizeof(query), mapping);
                    }
                    else
                    {
                        modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_NONE);
                        modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK);
                        modbus_close(ctx);
                        FD_CLR(i, &reads);
#ifdef _WIN32
                        closesocket(i);
#else
                        close(i);
#endif // _WIN32

                        m_errCount++;
                    }
                    if(m_errCount > 5)
                    {
                        m_initialized = false;
                        break;
                    }
                }
            }
        }
    }
    m_initialized = false;
}
