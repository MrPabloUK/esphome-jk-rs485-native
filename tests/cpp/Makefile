CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O0 -g
INCLUDES = -I../../components/jk_rs485_native_master
SRC = ../../components/jk_rs485_native_master/protocol.cpp \
      ../../components/jk_rs485_native_master/frame_parser.cpp \
      ../../components/jk_rs485_native_master/scheduler.cpp \
      test_native.cpp

.PHONY: test clean

test: test_native
	./test_native

test_native: $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SRC)

clean:
	rm -f test_native test_native.exe
