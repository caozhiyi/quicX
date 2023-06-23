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
				  ../src/quic/packet/header/*.cpp   \
				  ../src/quic/crypto/*.cpp          \
				  ../src/quic/crypto/tls/*.cpp      \
				  ../src/quic/udp/*.cpp             \
				  ../src/quic/controller/*.cpp      \
				  ../src/quic/connection/*.cpp      \
				  ../src/quic/process/*.cpp         \
				  ../src/quic/app/*.cpp             \
				  ./common/alloter/*.cpp            \
				  ./common/timer/*.cpp              \
				  ./common/lock/*.cpp               \
				  ./common/log/*.cpp                \
				  ./common/util/*.cpp               \
				  ./common/network/*.cpp            \
				  ./common/deocde/*.cpp             \
				  ./common/structure/*.cpp          \
				  ./common/buffer/*.cpp             \
				  ./quic/frame/*.cpp                \
				  ./quic/packet/*.cpp               \
				  ./quic/packet/header/*.cpp        \
				  ./quic/crypto/*.cpp               \
				  ./quic/connection/*.cpp           \
				  ./quic/stream/*.cpp               \
				  ./quic/include/*.cpp              \
				  ./common/*.cpp                    \
				  ./*.cpp)

OBJS = $(patsubst %.cpp, %.o, $(SRCS))

CC = g++

INCLUDES = -I../              \
		   -I../src/          \
           -I../third/        \
		   -I../third/boringssl/include  \

#debug
CCFLAGS = -fPIC -m64 -g -std=c++14 -lstdc++ -pipe  -lpthread

GTESTLIB = ../third/lib/libgtest.a
CRYPTOLIB = ../third/lib/libcrypto.a
SSLLIB = ../third/lib/libssl.a

BIN = quicx-utest

all:$(BIN)

$(BIN):$(OBJS)
	$(CC) $(OBJS) -o $(BIN) $(CCFLAGS) $(INCLUDES) $(GTESTLIB) ${SSLLIB} ${CRYPTOLIB}

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES) 

clean:
	rm -rf $(BIN) $(OBJS) *.log

test:
	./$(BIN)