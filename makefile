# the compiler : gcc for c program, define as g++ program for c++

CC = gcc

# compiler flags:
# -g adds debugging information to the executable file
# -Wall turns on most, but not all, compiler warnings

CFLAGS = -g -Wall -Wextra -pedantic -D_FILE_OFFSET_BITS=64 -lfuse -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
BIN = a.out
SOURCE1 = unified_filesystem.c aes.c

# the build target executable :

all : unified_filesystem

unified_filesystem: $(SOURCE1)
	$(CC) $(CFLAGS) $(SOURCE1) -o unified_filesystem -lfuse

clean:
	rm unified_filesystem

safe_run:
	./unified_filesystem --virtual_drive_path /home/zeroday/Desktop/cse496/bitirme/test --hard_drive /media/zeroday/merve

