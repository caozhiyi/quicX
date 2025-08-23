SRCS = $(shell find ../src -name "*.cpp") \
       $(shell find . -name "*.cpp")

OBJS = $(patsubst %.cpp, %.o, $(SRCS))

CC = g++

INCLUDES = -I../              \
		   -I../src/          \
           -I../third/        \
		   -I../third/boringssl/include  \

#debug
CCFLAGS = -fPIC -m64 -g -std=c++17 -lstdc++ -pipe  -lpthread

GTESTLIB = ../third/lib/libgtest.a
CRYPTOLIB = ../third/lib/libcrypto.a
SSLLIB = ../third/lib/libssl.a

BIN = quicx_utest

all:$(BIN)

$(BIN):$(OBJS)
	$(CC) $(OBJS) -o $(BIN) $(CCFLAGS) $(INCLUDES) $(GTESTLIB) ${SSLLIB} ${CRYPTOLIB}

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES) 

clean:
	rm -rf $(BIN) $(OBJS) *.log

test:
	./$(BIN)