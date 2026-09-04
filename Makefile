CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I.
CLIBS = -lpthread

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
	$(CXX) $(CXXFLAGS) -c $< -o $@

MD:
	mkdir -p $(BINDIR)

clean:
	rm -rf $(TARGET) $(OBJECTS) $(BINDIR)

.PHONY: all clean MD
