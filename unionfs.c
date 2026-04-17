#define FUSE_USE_VERSION 30
#include <unistd.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

// Declare your function (from test_path.c)
extern char* resolve_path(const char *path);


// ✅ getattr
static int fs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char *real = resolve_path(path);

    if (real == NULL)
        return -ENOENT;

    res = lstat(real, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}


// ✅ read
static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int fd;
    int res;

    char *real = resolve_path(path);

    if (real == NULL)
        return -ENOENT;

    fd = open(real, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}


// ✅ register functions
static struct fuse_operations operations = {
    .getattr = fs_getattr,
    .read = fs_read,
};


// ✅ main
int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &operations, NULL);
}
