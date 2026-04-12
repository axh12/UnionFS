CC = gcc
CFLAGS = -Wall -D_FILE_OFFSET_BITS=64
LIBS = -lfuse3

SRC = src/main.c
OUT = mini_unionfs

all:
	$(CC) $(SRC) -o $(OUT) $(LIBS) $(CFLAGS)

clean:
	rm -f $(OUT)
