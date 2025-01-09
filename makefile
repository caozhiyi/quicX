SRCS = $(shell find ./src -name "*.cpp")

OBJS = $(patsubst %.cpp, %.o, $(SRCS))

CC = g++

INCLUDES = -I./src/          \
           -I./third/        \
           -I./third/boringssl/include

#debug
CCFLAGS = -fPIC -m64 -g -std=c++14 -lstdc++ -pipe -lpthread

LIB = libquicx.a

all:$(LIB)

$(LIB):$(OBJS)
	ar rcs $(LIB) $(OBJS)

%.o : %.cpp
	$(CC) -c $< -o $@ $(CCFLAGS) $(INCLUDES)

clean:
	rm -rf $(LIB) $(OBJS)
