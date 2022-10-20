# modServer
A modbus server implemented by libmodbus.  
It can serve for multi client and easy to use for everyone.

It's hard for many person to use libmodbus.

It's very important to encapsulate it for easy using. 

# build
type 'make' to build program.

# usage 
see main.cpp 


start a modbus tcp server,  Add your code in fun() thread. 
you can write some value to the registers .


```
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
```


# dependency
- libmodbus

```
sudo apt install libmodbus-dev
```


# reference
https://blog.csdn.net/qq_38158479/article/details/122630336

