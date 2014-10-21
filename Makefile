CXXFLAGS = -Wall -pipe -std=c++98 -fno-rtti -fno-exceptions -Wno-long-long -Wno-deprecated -g -DQCC_OS_LINUX -DQCC_OS_GROUP_POSIX -DQCC_CPU_ARM
LIBS = -lalljoyn -lajrouter -lstdc++ -lcrypto -lpthread -lrt
INCLUDES = /usr/local/include
.PHONY: default clean
ROUTER_OBJ=/usr/local/lib/BundledRouter.o
default:    all

all:    led_service led_client

led_service:    led_service.o
	$(CXX) -o $@ led_service.o $(ROUTER_OBJ) $(LIBS) -lwiringPi

led_service.o:  led_service.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ led_service.cpp

led_client: led_client.o
	$(CXX) -o $@ led_client.o $(ROUTER_OBJ) $(LIBS)

led_client.o:  led_client.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ led_client.cpp

clean:
	rm -f *.o
	rm -f led_service
	rm -f led_client    
