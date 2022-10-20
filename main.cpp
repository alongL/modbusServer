#include <iostream>
#include "RDSModbusSlave.h"

using namespace std;




void fun(RDSModbusSlave* server)
{
	int ret=0;
	srand(time(0));

   
	while(1)
	{
		
        uint16_t value = 1000+ rand()%100;
		cout<<"write value:"<<value <<endl;
        
        ret = server->setInputRegisterValue(10, value);
		ret = server->setRegisterValue(10, value);
		
        float valuef = value / 100.0;
        server->setRegisterFloatValue(12, valuef);
		
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

}



int main()
{
    RDSModbusSlave modServer(1502);
    modServer.run();
    
    std::thread td(fun, &modServer);
	td.join();

    return 0;
}
