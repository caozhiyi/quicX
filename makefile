SRCS_QUIC = $(shell find ./src/quic -name "*.cpp") \
            $(shell find ./src/common -name "*.cpp")

SRCS_HTTP3 = $(shell find ./src/http3 -name "*.cpp")

OBJS_QUIC = $(patsubst %.cpp, %.o, $(SRCS_QUIC))
OBJS_HTTP3 = $(patsubst %.cpp, %.o, $(SRCS_HTTP3))

INCLUDES = -I./src/          \
           -I./third/        \
           -I./third/boringssl/include

CC = g++

#debug
CCFLAGS = -fPIC -m64 -g -std=c++14 -lstdc++ -pipe -lpthread

LIB_QUIC = libquicx.a
LIB_HTTP3 = libhttp3.a

all: $(LIB_QUIC) $(LIB_HTTP3)

$(LIB_QUIC): $(OBJS_QUIC)
	ar rcs $(LIB_QUIC) $(OBJS_QUIC)

$(LIB_HTTP3): $(OBJS_HTTP3)
	ar rcs $(LIB_HTTP3) $(OBJS_HTTP3) $(OBJS_QUIC)

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES)

clean:
	rm -rf $(LIB_QUIC) $(LIB_HTTP3) $(OBJS_QUIC) $(OBJS_HTTP3)
