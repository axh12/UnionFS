#define FUSE_USE_VERSION 26

#include <fuse.h>
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

/**
 * Constructs a full path from base directory and relative path
 */
void construct_path(const char *base_dir, const char *rel_path, char *full_path, size_t max_len) {
    snprintf(full_path, max_len, "%s%s", base_dir, rel_path);
}

/**
 * Constructs the whiteout filename for a given path
 * Example: "/config.txt" -> "/.wh.config.txt"
 */
void get_whiteout_path(const char *path, char *wh_path, size_t max_len) {
    const char *filename = strrchr(path, '/');
    if (filename == NULL) {
        filename = path;
    } else {
        filename++;
    }

    size_t dir_len = filename - path;
    snprintf(wh_path, max_len, "%.*s.wh.%s", (int)dir_len, path, filename);
}

/**
 * Resolves a path according to the union filesystem logic:
 * 1. Check if whiteout file exists in upper_dir -> return ENOENT
 * 2. Check if file exists in upper_dir -> return upper path
 * 3. Check if file exists in lower_dir -> return lower path
 * 4. Otherwise -> return ENOENT
 * 
 * Returns: 0 on success, -ENOENT if file not found
 */
int resolve_path(const char *fuse_path, char *resolved_path, int *layer) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    struct stat st;
    char upper_path[4096];
    char lower_path[4096];
    char wh_path[4096];

    // Construct full paths
    construct_path(data->upper_dir, fuse_path, upper_path, sizeof(upper_path));
    construct_path(data->lower_dir, fuse_path, lower_path, sizeof(lower_path));
    get_whiteout_path(fuse_path, wh_path, sizeof(wh_path));

    // Check for whiteout file
    if (lstat(wh_path, &st) == 0) {
        return -ENOENT;
    }

    // Check upper_dir
    if (lstat(upper_path, &st) == 0) {
        strcpy(resolved_path, upper_path);
        *layer = 1;  // Upper layer
        return 0;
    }

    // Check lower_dir
    if (lstat(lower_path, &st) == 0) {
        strcpy(resolved_path, lower_path);
        *layer = 0;  // Lower layer
        return 0;
    }

    return -ENOENT;
}

/**
 * Copy-on-Write: Copies a file from lower_dir to upper_dir
 * Preserves file permissions and metadata
 */
int copy_on_write(const char *fuse_path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[4096];
    char upper_path[4096];
    int layer;

    // Resolve the file location
    if (resolve_path(fuse_path, resolved_path, &layer) != 0) {
        return -ENOENT;
    }

    // If file is already in upper_dir, no need to copy
    if (layer == 1) {
        return 0;  // Already in upper layer
    }

    // If file is in lower_dir, copy it to upper_dir
    construct_path(data->upper_dir, fuse_path, upper_path, sizeof(upper_path));

    // Ensure parent directory exists in upper_dir
    char parent_dir[4096];
    strcpy(parent_dir, upper_path);
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        mkdir(parent_dir, 0755);  // Create parent if it doesn't exist
    }

    // Copy file from lower to upper
    struct stat st;
    if (lstat(resolved_path, &st) != 0) {
        return -EIO;
    }

    // Read source file
    FILE *src = fopen(resolved_path, "rb");
    if (src == NULL) {
        return -EIO;
    }

    // Write to destination file
    FILE *dst = fopen(upper_path, "wb");
    if (dst == NULL) {
        fclose(src);
        return -EIO;
    }

    // Copy file contents
    char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dst) != bytes) {
            fclose(src);
            fclose(dst);
            unlink(upper_path);
            return -EIO;
        }
    }

    fclose(src);
    fclose(dst);

    // Preserve file permissions
    chmod(upper_path, st.st_mode & 07777);

    return 0;
}

// ============================================================================
// FUSE OPERATIONS
// ============================================================================

/**
 * Get file attributes
 */
static int unionfs_getattr(const char *path, struct stat *stbuf) {
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0) {
        return -ENOENT;
    }

    if (lstat(resolved_path, stbuf) != 0) {
        return -errno;
    }

    return 0;
}

/**
 * Open file - CRITICAL FOR CoW
 * Trigger Copy-on-Write if file is being opened for writing
 */
static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved_path[4096];
    int layer;
    int ret;

    // Check if file exists and which layer it's in
    if (resolve_path(path, resolved_path, &layer) != 0) {
        // File doesn't exist - check if we can create it in upper_dir
        if ((fi->flags & O_CREAT)) {
            return 0;  // Allow create
        }
        return -ENOENT;
    }

    // If opening for writing and file is in lower_dir, trigger CoW
    if ((fi->flags & (O_WRONLY | O_RDWR | O_APPEND)) && layer == 0) {
        ret = copy_on_write(path);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

/**
 * Read from file
 */
static int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0) {
        return -ENOENT;
    }

    int fd = open(resolved_path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }

    int res = pread(fd, buf, size, offset);
    close(fd);

    if (res < 0) {
        return -errno;
    }

    return res;
}

/**
 * Write to file
 */
static int unionfs_write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
    char resolved_path[4096];
    int layer;

    // Resolve path (should already be copied by open if from lower_dir)
    if (resolve_path(path, resolved_path, &layer) != 0) {
        return -ENOENT;
    }

    int fd = open(resolved_path, O_WRONLY);
    if (fd < 0) {
        return -errno;
    }

    int res = pwrite(fd, buf, size, offset);
    close(fd);

    if (res < 0) {
        return -errno;
    }

    return res;
}

/**
 * Create a new file
 */
static int unionfs_create(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[4096];

    construct_path(data->upper_dir, path, upper_path, sizeof(upper_path));

    // Ensure parent directory exists
    char parent_dir[4096];
    strcpy(parent_dir, upper_path);
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        mkdir(parent_dir, 0755);
    }

    int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd < 0) {
        return -errno;
    }

    close(fd);
    return 0;
}

/**
 * List directory contents
 */
static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[4096], lower_path[4096];
    DIR *upper_dp, *lower_dp;
    struct dirent *entry;

    construct_path(data->upper_dir, path, upper_path, sizeof(upper_path));
    construct_path(data->lower_dir, path, lower_path, sizeof(lower_path));

    // Read from upper_dir
    upper_dp = opendir(upper_path);
    if (upper_dp != NULL) {
        while ((entry = readdir(upper_dp)) != NULL) {
            filler(buf, entry->d_name, NULL, 0);
        }
        closedir(upper_dp);
    }

    // Read from lower_dir, but skip whiteout entries
    lower_dp = opendir(lower_path);
    if (lower_dp != NULL) {
        while ((entry = readdir(lower_dp)) != NULL) {
            // Skip whiteout files and entries already in upper_dir
            if (strncmp(entry->d_name, ".wh.", 4) == 0) {
                continue;
            }

            char full_upper_path[4096];
            construct_path(data->upper_dir, path, full_upper_path, sizeof(full_upper_path));
            strcat(full_upper_path, "/");
            strcat(full_upper_path, entry->d_name);

            struct stat st;
            if (lstat(full_upper_path, &st) != 0) {
                // File not in upper_dir, so add from lower_dir
                filler(buf, entry->d_name, NULL, 0);
            }
        }
        closedir(lower_dp);
    }

    return 0;
}

/**
 * Unlink (delete) file
 * Creates whiteout if file is in lower_dir
 */
static int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char resolved_path[4096];
    char upper_path[4096];
    char wh_path[4096];
    int layer;

    // Resolve the file
    if (resolve_path(path, resolved_path, &layer) != 0) {
        return -ENOENT;
    }

    if (layer == 1) {
        // File is in upper_dir, physically delete it
        if (unlink(resolved_path) != 0) {
            return -errno;
        }
        return 0;
    } else {
        // File is in lower_dir, create whiteout in upper_dir
        construct_path(data->upper_dir, path, upper_path, sizeof(upper_path));
        get_whiteout_path(path, wh_path, sizeof(wh_path));

        // Ensure parent directory exists
        char parent_dir[4096];
        strcpy(parent_dir, wh_path);
        char *last_slash = strrchr(parent_dir, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            mkdir(parent_dir, 0755);
        }

        // Create empty whiteout file
        int fd = open(wh_path, O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            return -errno;
        }
        close(fd);

        return 0;
    }
}

/**
 * Create directory
 */
static int unionfs_mkdir(const char *path, mode_t mode) {
    struct mini_unionfs_state *data = UNIONFS_DATA;
    char upper_path[4096];

    construct_path(data->upper_dir, path, upper_path, sizeof(upper_path));

    if (mkdir(upper_path, mode) != 0) {
        return -errno;
    }

    return 0;
}

/**
 * Remove directory
 */
static int unionfs_rmdir(const char *path) {
    char resolved_path[4096];
    int layer;

    if (resolve_path(path, resolved_path, &layer) != 0) {
        return -ENOENT;
    }

    if (layer == 1) {
        // Directory is in upper_dir, physically remove it
        if (rmdir(resolved_path) != 0) {
            return -errno;
        }
        return 0;
    }

    // Directory is in lower_dir - for now, prevent removal
    return -EACCES;
}

// ============================================================================
// FUSE OPERATIONS STRUCTURE
// ============================================================================

static struct fuse_operations unionfs_oper = {
    .getattr     = unionfs_getattr,
    .open        = unionfs_open,
    .read        = unionfs_read,
    .write       = unionfs_write,
    .create      = unionfs_create,
    .readdir     = unionfs_readdir,
    .unlink      = unionfs_unlink,
    .mkdir       = unionfs_mkdir,
    .rmdir       = unionfs_rmdir,
};

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    state->lower_dir = argv[1];
    state->upper_dir = argv[2];

    // Shift argv to remove our custom arguments
    argv[1] = argv[3];
    argc = 2;

    fprintf(stderr, "Mini-UnionFS mounted:\n");
    fprintf(stderr, "  Lower (read-only): %s\n", state->lower_dir);
    fprintf(stderr, "  Upper (read-write): %s\n", state->upper_dir);
    fprintf(stderr, "  Mount point: %s\n", argv[1]);

    return fuse_main(argc, argv, &unionfs_oper, state);
}
