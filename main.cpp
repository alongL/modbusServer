#include <iostream>
#include "RDSModbusSlave.h"
#include <thread>

using namespace std;

void fun(RDSModbusSlave* server);




int main(int argc, char* argv[])
{
    string portStr = "1502";
    if(argc>1)
        portStr = argv[1];

    int port = atoi(portStr.c_str());

    cout<<"modbus server listen on port:"<< port << endl;
    RDSModbusSlave modServer("0.0.0.0", port);
    modServer.run();
    
    if(argc = 2)
    {
        std::thread td(fun, &modServer);
	    td.join();
    }

    int flag=0;
    while (1)
    {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        flag++;
    }



    std::cout << "exit modbusServer" << std::endl;
    return 0;
}

















void fun(RDSModbusSlave* server)
{
	int ret=0;
	srand(time(0));
	while(1)
	{
		
        uint16_t value = 1000 + (rand() % 1000);
		cout<<"write value:"<<value <<endl;
        
        ret = server->setInputRegisterValue(10, value);
		ret = server->setHoldingRegisterValue(10, value);
        server->setHoldingRegisterValue(30010, value);
		
        float valuef = value / 100.0;
		server->setInputRegisterValue(12, valuef);
        server->setHoldingRegisterValue(12, valuef);
        server->setHoldingRegisterValue(30012, valuef);

        server->setTab_Input_Bits(30010, 1);
        server->setTab_Input_Bits(30011, 1);

        
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

}
