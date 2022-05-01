SRCS = $(wildcard ../src/common/timer/*.cpp         \
                  ../src/common/util/*.cpp          \
				  ../src/common/queue/*.cpp         \
				  ../src/common/lock/*.cpp          \
				  ../src/common/buffer/*.cpp        \
				  ../src/common/alloter/*.cpp       \
				  ../src/common/network/*.cpp       \
				  ../src/common/decode/*.cpp        \
				  ../src/common/log/*.cpp           \
				  ../src/common/os/posix/*.cpp      \
				  ../src/common/network/linux/*.cpp \
				  ../src/common/network/mac/*.cpp   \
				  ../src/quic/common/*.cpp          \
				  ../src/quic/frame/*.cpp           \
				  ../src/quic/stream/*.cpp          \
				  ../src/quic/packet/*.cpp          \
				  ../src/quic/crypto/*.cpp          \
				  ../src/quic/udp/*.cpp             \
				  ../src/quic/controller/*.cpp      \
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
CRYPTOLIB = ../third/lib/libcrypto.a
SSLLIB = ../third/lib/libssl.a

BIN = quicx-test

all:$(BIN)

$(BIN):$(OBJS)
	$(CC) $(OBJS) -o $(BIN) $(CCFLAGS) $(INCLUDES) $(GTESTLIB) ${SSLLIB} ${CRYPTOLIB}

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES) 

clean:
	rm -rf $(BIN) $(OBJS) *.log

test:
	./$(BIN)