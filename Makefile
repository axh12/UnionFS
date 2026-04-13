CC = gcc
CFLAGS = -Wall -D_FILE_OFFSET_BITS=64
LIBS = `pkg-config fuse3 --cflags --libs`

SRC = src/main.c src/unionfs_cow.c
OUT = mini_unionfs

all:
	$(CC) $(SRC) -o $(OUT) $(LIBS) $(CFLAGS)

clean:
	rm -f $(OUT)
