# modbusServer
A modbus server implemented by libmodbus.  
It can serve for multi client and easy to use for everyone.  
It's hard for many person to use libmodbus.  
It's very important to encapsulate it for easy using. 

# build
in Linux, just type 'make' to build program.
It can also be used in Windows, but I didn't test.

# usage 
see main.cpp 

start a modbus tcp server,  Add your code in fun() thread. 
you can write some value to the registers.

```
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


int main()
{
    RDSModbusSlave modServer(1502);
    modServer.run();
    
    std::thread td(fun, &modServer);
	  td.join();

    return 0;
}
```


# dependency
- libmodbus-dev

```
sudo apt install libmodbus-dev
```


# reference
https://github.com/stephane/libmodbus/blob/master/tests/bandwidth-server-many-up.c
https://blog.csdn.net/qq_38158479/article/details/122630336

