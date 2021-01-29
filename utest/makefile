SRCS = $(wildcard ../common/timer/*.cpp ../common/util/*.cpp ./common/timer/*.cpp ./common/util/*.cpp ./*.cpp)

OBJS = $(patsubst %.cpp, %.o, $(SRCS))

CC = g++

INCLUDES = -I../              \
		   -I../common/       \
           -I../third/gtest   \

#debug
CCFLAGS = -fPIC -m64 -g -std=c++11 -lstdc++ -pipe 

GTESTLIB = ../third/lib/libgtest.a
BIN = quicx-utest

all:$(BIN)

$(BIN):$(OBJS)
	$(CC) $(OBJS) -o $(BIN) $(CCFLAGS) $(INCLUDES) $(GTESTLIB)

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES) 

clean:
	rm -rf $(BIN) $(OBJS)

test:
	./$(BIN)