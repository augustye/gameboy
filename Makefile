GCC=g++
CPP_FLAGS= -fPIC
OS:=$(shell uname)

all: gameboy.so

gameboy.so: gameboy.o gameboy.h 
	gcc gameboy.o ${CPP_FLAGS} -shared -o $@

%.o: %.cpp
	${GCC} ${CPP_FLAGS} -c $< -o $@

clean:
	rm -rf *.o gameboy gameboy.so
