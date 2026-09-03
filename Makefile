CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

# 本地内置 libmodbus 静态库支持（彻底摆脱系统依赖）
LIBMODBUS_DIR = ./3rdparty/install
INCLUDE_DIRS = -I. -I$(LIBMODBUS_DIR)/include

ifneq ($(wildcard $(LIBMODBUS_DIR)/lib/libmodbus.a),)
    CLIBS = $(LIBMODBUS_DIR)/lib/libmodbus.a -lpthread
else
    CLIBS = -lmodbus -lpthread
endif

ver ?= release
ifeq ($(ver), debug)
    CXXFLAGS += -g -O0
else
    CXXFLAGS += -O2
endif

SRC = $(wildcard *.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(SRC))
PROGRAM = modServer
BINDIR = ./bin
TARGET = $(BINDIR)/$(PROGRAM)

all: MD $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $(TARGET) $(OBJECTS) $(CLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -c $< -o $@

MD:
	mkdir -p $(BINDIR)

clean:
	rm -rf $(TARGET) $(OBJECTS) $(BINDIR)

.PHONY: all clean MD
