all:
	gcc -Wall -o mini_unionfs whiteout.c `pkg-config fuse3 --cflags --libs`

clean:
	rm -f mini_unionfs
