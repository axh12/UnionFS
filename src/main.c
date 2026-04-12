#define FUSE_USE_VERSION 30

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "state.h"
#include "operations.h"

static int dummy_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    return -ENOENT;
}

static int dummy_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    return -ENOENT;
}
static int dummy_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags) {

    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    return 0;
}
// FUSE operations (HOOKS for teammates)
static struct fuse_operations unionfs_oper = {
    .getattr = dummy_getattr,
    .read = dummy_read,
    .readdir = dummy_readdir,
};

int main(int argc, char *argv[]) {

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mountpoint>\n", argv[0]);
        exit(1);
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));

    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    if (!state->lower_dir || !state->upper_dir) {
        perror("realpath");
        exit(1);
    }

    // SHIFT arguments correctly for FUSE3
    for (int i = 1; i < argc - 2; i++) {
        argv[i] = argv[i + 2];
    }

    argc = argc - 2;

    return fuse_main(argc, argv, &unionfs_oper, state);
}
