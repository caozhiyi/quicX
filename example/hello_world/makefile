SRCS_CLIENT = client.cpp
SRCS_SERVER = server.cpp

OBJS_CLIENT = $(patsubst %.cpp, %.o, $(SRCS_CLIENT))
OBJS_SERVER = $(patsubst %.cpp, %.o, $(SRCS_SERVER))

CC = g++

INCLUDES = -I./          \
           -I../../src   \

#debug
CCFLAGS = -fPIC -m64 -g -std=c++14 -lstdc++ -pipe -lpthread

QUICXLIB = ../../libquicx.a
CRYPTOLIB = ../../third/lib/libcrypto.a
SSLLIB = ../../third/lib/libssl.a

BIN_CLIENT = client
BIN_SERVER = server

all: $(BIN_CLIENT) $(BIN_SERVER)

$(BIN_CLIENT): $(OBJS_CLIENT)
	$(CC) $(OBJS_CLIENT) -o $(BIN_CLIENT) $(CCFLAGS) $(INCLUDES) $(QUICXLIB) $(SSLLIB) $(CRYPTOLIB)

$(BIN_SERVER): $(OBJS_SERVER) 
	$(CC) $(OBJS_SERVER) -o $(BIN_SERVER) $(CCFLAGS) $(INCLUDES) $(QUICXLIB) $(SSLLIB) $(CRYPTOLIB)

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES)

clean:
	rm -rf $(BIN_CLIENT) $(BIN_SERVER) $(OBJS_CLIENT) $(OBJS_SERVER)
