#ifndef OPERATIONS_H
#define OPERATIONS_H

#include <fuse3/fuse.h>
#include <sys/stat.h>

// FUSE3 function declarations

int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi);

int unionfs_read(const char *path, char *buf, size_t size,
                 off_t offset, struct fuse_file_info *fi);

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags);

#endif
