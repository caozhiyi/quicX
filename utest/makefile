SRCS = $(wildcard ../src/common/timer/*.cpp   \
                  ../src/common/util/*.cpp    \
				  ../src/common/queue/*.cpp   \
				  ../src/common/lock/*.cpp    \
				  ../src/common/buffer/*.cpp  \
				  ../src/common/alloter/*.cpp \
				  ../src/common/network/*.cpp \
				  ../src/common/network/linux/*.cpp \
				  ./common/timer/*.cpp    \
				  ./common/lock/*.cpp     \
				  ./common/util/*.cpp     \
				  ./common/network/*.cpp  \
				  ./common/*.cpp          \
				  ./*.cpp)

OBJS = $(patsubst %.cpp, %.o, $(SRCS))

CC = g++

INCLUDES = -I../              \
		   -I../src/          \
		   -I../src/common/   \
           -I../third/        \

#debug
CCFLAGS = -fPIC -m64 -g -std=c++11 -lstdc++ -pipe  -lpthread

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