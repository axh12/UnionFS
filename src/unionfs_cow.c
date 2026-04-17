#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *)fuse_get_context()->private_data)

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void construct_path(const char *base_dir, const char *rel_path, char *full_path, size_t max_len) {
    snprintf(full_path, max_len, "%s%s", base_dir, rel_path);
}

// Build whiteout path inside upper_dir for a given fuse path
// e.g. "/test.txt" -> "/home/.../upper/.wh.test.txt"
static void build_whiteout_path(const char *fuse_path, char *wh_path, size_t max_len) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    const char *slash = strrchr(fuse_path, '/');
    const char *fname = (slash) ? slash + 1 : fuse_path;

    if (slash && slash != fuse_path) {
        char dir[4096];
        int dlen = slash - fuse_path;
        snprintf(dir, sizeof(dir), "%.*s", dlen, fuse_path);
        snprintf(wh_path, max_len, "%s%s/.wh.%s", data->upper_dir, dir, fname);
    } else {
        snprintf(wh_path, max_len, "%s/.wh.%s", data->upper_dir, fname);
    }
}

int resolve_path(const char *fuse_path, char *resolved_path, int *layer) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    struct stat st;
    char upper_path[4096];
    char lower_path[4096];
    char wh_path[4096];

    snprintf(upper_path, sizeof(upper_path), "%s%s", data->upper_dir, fuse_path);
    snprintf(lower_path, sizeof(lower_path), "%s%s", data->lower_dir, fuse_path);

    // Check for whiteout in upper
    build_whiteout_path(fuse_path, wh_path, sizeof(wh_path));
    if (lstat(wh_path, &st) == 0)
        return -ENOENT;

    // Check upper
    if (lstat(upper_path, &st) == 0) {
        strcpy(resolved_path, upper_path);
        *layer = 1;
        return 0;
    }

    // Check lower
    if (lstat(lower_path, &st) == 0) {
        strcpy(resolved_path, lower_path);
        *layer = 0;
        return 0;
    }

    return -ENOENT;
}

int copy_on_write(const char *fuse_path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[4096];
    char upper_path[4096];
    int layer;

    if (resolve_path(fuse_path, resolved_path, &layer) != 0)
        return -ENOENT;

    if (layer == 1)
        return 0;

    snprintf(upper_path, sizeof(upper_path), "%s%s", data->upper_dir, fuse_path);

    char parent_dir[4096];
    strcpy(parent_dir, upper_path);
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(parent_dir, 0755);
    }

    struct stat st;
    if (lstat(resolved_path, &st) != 0)
        return -EIO;

    FILE *src = fopen(resolved_path, "rb");
    if (!src) return -EIO;

    FILE *dst = fopen(upper_path, "wb");
    if (!dst) { fclose(src); return -EIO; }

    char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dst) != bytes) {
            fclose(src); fclose(dst);
            unlink(upper_path);
            return -EIO;
        }
    }
    fclose(src);
    fclose(dst);
    chmod(upper_path, st.st_mode & 07777);
    return 0;
}

// ============================================================================
// FUSE OPERATIONS
// ============================================================================

int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;

    if (strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) == 0) {
        if (lstat(resolved_path, stbuf) == 0)
            return 0;
    }

    return -ENOENT;
}

int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0) {
        if (fi->flags & O_CREAT)
            return 0;
        return -ENOENT;
    }

    if ((fi->flags & (O_WRONLY | O_RDWR | O_APPEND)) && layer == 0) {
        int ret = copy_on_write(path);
        if (ret != 0) return ret;
    }

    return 0;
}

int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi) {
    (void) fi;
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0)
        return -ENOENT;

    int fd = open(resolved_path, O_RDONLY);
    if (fd < 0) return -errno;

    int res = pread(fd, buf, size, offset);
    close(fd);
    return (res < 0) ? -errno : res;
}

int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0)
        return -ENOENT;

    int fd = open(resolved_path, O_WRONLY);
    if (fd < 0) return -errno;

    int res = pwrite(fd, buf, size, offset);
    close(fd);
    return (res < 0) ? -errno : res;
}

int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[4096];

    snprintf(upper_path, sizeof(upper_path), "%s%s", data->upper_dir, path);

    char parent_dir[4096];
    strcpy(parent_dir, upper_path);
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(parent_dir, 0755);
    }

    int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd < 0) return -errno;
    close(fd);
    return 0;
}

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    struct mini_unionfs_state *data = UNIONFS_DATA;

    char upper_path[4096], lower_path[4096];

    snprintf(upper_path, sizeof(upper_path), "%s%s", data->upper_dir, path);
    snprintf(lower_path, sizeof(lower_path), "%s%s", data->lower_dir, path);

    DIR *upper_dp = opendir(upper_path);
    DIR *lower_dp = opendir(lower_path);

    struct dirent *entry;

    // Required entries
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // 🔹 UPPER LAYER FIRST
    if (upper_dp) {
        while ((entry = readdir(upper_dp)) != NULL) {

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            if (strncmp(entry->d_name, ".wh.", 4) == 0)
                continue;

            filler(buf, entry->d_name, NULL, 0, 0);
        }
        closedir(upper_dp);
    }

    // 🔹 LOWER LAYER (with whiteout filtering)
    if (lower_dp) {
        while ((entry = readdir(lower_dp)) != NULL) {

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            if (strncmp(entry->d_name, ".wh.", 4) == 0)
                continue;

            // check whiteout in upper
            char wh_path[4096];
            snprintf(wh_path, sizeof(wh_path), "%s/.wh.%s",
                     upper_path, entry->d_name);

            struct stat st;
            if (lstat(wh_path, &st) == 0)
                continue;

            //  avoid duplicate (already in upper)
            char upper_file[4096];
            snprintf(upper_file, sizeof(upper_file), "%s/%s",
                     upper_path, entry->d_name);

            if (lstat(upper_file, &st) == 0)
                continue;

            filler(buf, entry->d_name, NULL, 0, 0);
        }
        closedir(lower_dp);
    }

    return 0;
}

int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0)
        return -ENOENT;

    // If in upper, delete it physically
    if (layer == 1) {
        if (unlink(resolved_path) != 0)
            return -errno;
    }

    // Always create whiteout to hide lower layer version
    char wh_path[4096];
    build_whiteout_path(path, wh_path, sizeof(wh_path));

    int fd = open(wh_path, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) return -errno;
    close(fd);
    return 0;
}

int unionfs_mkdir(const char *path, mode_t mode) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[4096];

    snprintf(upper_path, sizeof(upper_path), "%s%s", data->upper_dir, path);
    if (mkdir(upper_path, mode) != 0)
        return -errno;
    return 0;
}

int unionfs_rmdir(const char *path) {
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0)
        return -ENOENT;

    if (layer == 1) {
        if (rmdir(resolved_path) != 0)
            return -errno;
        return 0;
    }

    return -EACCES;
}
