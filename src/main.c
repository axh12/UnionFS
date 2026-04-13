#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "state.h"
#include "operations.h"

// Forward declarations (implemented in unionfs_cow.c)
int unionfs_getattr(const char *, struct stat *, struct fuse_file_info *);
int unionfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
                    struct fuse_file_info *, enum fuse_readdir_flags);
int unionfs_read(const char *, char *, size_t, off_t, struct fuse_file_info *);
int unionfs_open(const char *, struct fuse_file_info *);
int unionfs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
int unionfs_create(const char *, mode_t, struct fuse_file_info *);
int unionfs_unlink(const char *);
int unionfs_mkdir(const char *, mode_t);
int unionfs_rmdir(const char *);

// FUSE operations — all wired to real implementations
static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .create  = unionfs_create,
    .readdir = unionfs_readdir,
    .unlink  = unionfs_unlink,
    .mkdir   = unionfs_mkdir,
    .rmdir   = unionfs_rmdir,
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

    // Shift arguments so FUSE sees: program mountpoint [fuse-opts]
    for (int i = 1; i < argc - 2; i++) {
        argv[i] = argv[i + 2];
    }
    argc -= 2;

    return fuse_main(argc, argv, &unionfs_oper, state);
}
