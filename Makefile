GCC=gcc
C_FLAGS=-Ofast -fPIC
OS:=$(shell uname)

all: gameboy.so

gameboy.so: gameboy.o gameboy.h 
	gcc gameboy.o ${C_FLAGS} -shared -o $@

%.o: %.c
	${GCC} ${C_FLAGS} -c $< -o $@

clean:
	rm -rf *.o gameboy gameboy.so
