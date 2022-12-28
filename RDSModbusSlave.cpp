#include "RDSModbusSlave.h"
#include <string.h>

#ifdef WIN32
typedef int socklen_t;
#endif


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
RDSModbusSlave::RDSModbusSlave(string host, uint16_t port)
{
    initModbus(host, port, false);
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
bool RDSModbusSlave::setHoldingRegisterValue(int registerStartaddress, uint16_t Value)
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
uint16_t RDSModbusSlave::getHoldingRegisterValue(int registerStartaddress)
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
bool RDSModbusSlave::setHoldingRegisterValue(int registerStartaddress, float Value)
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


bool RDSModbusSlave::setInputRegisterValue(int registerStartaddress, float Value)
{
    if (registerStartaddress > (m_numRegisters - 2))
    {
        return false;
    }
    /*小端模式*/
    slavemutex.lock();
    modbus_set_float(Value, &mapping->tab_input_registers[registerStartaddress]);
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
float RDSModbusSlave::getHoldingRegisterFloatValue(int registerStartaddress)
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
    int master_socket;
    int rc;
    fd_set refset;
    fd_set rdset;
    /* Maximum file descriptor number */
    int fdmax;
    /* Clear the reference set of socket */
    FD_ZERO(&refset);
    /* Add the server socket */
    FD_SET(m_modbusSocket, &refset);

    /* Keep track of the max file descriptor */
    fdmax = m_modbusSocket;

    while( true ) 
    {
        rdset = refset;
        if (select(fdmax+1, &rdset, NULL, NULL, NULL) == -1) 
        {
            perror("Server select() failure.");
            break;
        }

        /* Run through the existing connections looking for data to be
         * read */
        for (master_socket = 0; master_socket <= fdmax; master_socket++) 
        {
            if (!FD_ISSET(master_socket, &rdset)) 
            {
                continue;
            }

            if (master_socket == m_modbusSocket) 
            {
                /* A client is asking a new connection */
                socklen_t addrlen;
                struct sockaddr_in clientaddr;
                int newfd;

                /* Handle new connections */
                addrlen = sizeof(clientaddr);
                memset(&clientaddr, 0, sizeof(clientaddr));
                newfd = accept(m_modbusSocket, (struct sockaddr *)&clientaddr, &addrlen);
                if (newfd == -1) 
                {
                    perror("Server accept() error");
                } else 
                {
                    FD_SET(newfd, &refset);

                    if (newfd > fdmax) 
                    {
                        /* Keep track of the maximum */
                        fdmax = newfd;
                    }
                    printf("New connection from %s:%d on socket %d\n", inet_ntoa(clientaddr.sin_addr), clientaddr.sin_port, newfd);
                }
            } else 
            {
                modbus_set_socket(ctx, master_socket);
                rc = modbus_receive(ctx, query);
                if (rc > 0) 
                {
                    modbus_reply(ctx, query, rc, mapping);
                } else if (rc == -1) 
                {
                    /* This example server in ended on connection closing or
                     * any errors. */
                    printf("Connection closed on socket %d\n", master_socket);
#ifdef _WIN32
                    closesocket(master_socket);
#else
                    close(master_socket);
#endif
                    /* Remove from reference set */
                    FD_CLR(master_socket, &refset);

                    if (master_socket == fdmax) 
                    {
                        fdmax--;
                    }
                }
            }
        }
    }
    m_initialized = false;
}
