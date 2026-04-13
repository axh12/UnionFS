#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>

// NOTE: this struct is defined by Person 1
// included here so your code compiles standalone for testing
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA \
    ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* ─────────────────────────────────────────
   HELPER: build the whiteout path
   example:
     path = "/delete_me.txt"
     result = upper_dir/.wh.delete_me.txt
───────────────────────────────────────── */
static void build_whiteout_path(const char *path, char *whiteout_path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    char path_copy1[PATH_MAX];
    char path_copy2[PATH_MAX];
    strncpy(path_copy1, path, PATH_MAX);
    strncpy(path_copy2, path, PATH_MAX);

    char *dir  = dirname(path_copy1);
    char *base = basename(path_copy2);

    if (strcmp(dir, "/") == 0)
        snprintf(whiteout_path, PATH_MAX, "%s/.wh.%s",
                 state->upper_dir, base);
    else
        snprintf(whiteout_path, PATH_MAX, "%s%s/.wh.%s",
                 state->upper_dir, dir, base);
}

/* ─────────────────────────────────────────
   HELPER: check if a file is whiteout'd
   returns 1 if whiteout exists, 0 if not
───────────────────────────────────────── */
static int is_whiteout(const char *dir_path, const char *filename) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char wh_path[PATH_MAX];

    if (strcmp(dir_path, "/") == 0)
        snprintf(wh_path, PATH_MAX, "%s/.wh.%s",
                 state->upper_dir, filename);
    else
        snprintf(wh_path, PATH_MAX, "%s%s/.wh.%s",
                 state->upper_dir, dir_path, filename);

    return access(wh_path, F_OK) == 0;
}

/* ─────────────────────────────────────────
   UNLINK: handles rm on a file
───────────────────────────────────────── */
static int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    char whiteout_path[PATH_MAX];

    snprintf(upper_path,  PATH_MAX, "%s%s", state->upper_dir, path);
    snprintf(lower_path,  PATH_MAX, "%s%s", state->lower_dir, path);
    build_whiteout_path(path, whiteout_path);

    // Case 1: file exists in upper → delete it physically
    if (access(upper_path, F_OK) == 0) {
        if (unlink(upper_path) == -1) return -errno;
        // if same file also in lower, create whiteout
        // so it doesn't reappear after deletion
        if (access(lower_path, F_OK) == 0) {
            int fd = open(whiteout_path, O_CREAT | O_WRONLY, 0644);
            if (fd != -1) close(fd);
        }
        return 0;
    }

    // Case 2: file only in lower → create whiteout marker
    if (access(lower_path, F_OK) == 0) {
        int fd = open(whiteout_path, O_CREAT | O_WRONLY, 0644);
        if (fd == -1) return -errno;
        close(fd);
        return 0;
    }

    // Case 3: file doesn't exist anywhere
    return -ENOENT;
}

/* ─────────────────────────────────────────
   READDIR: merged directory listing
   filters out whiteout'd files
───────────────────────────────────────── */
static int unionfs_readdir(const char *path, void *buf,
                           fuse_fill_dir_t filler, off_t offset,
                           struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    snprintf(upper_path, PATH_MAX, "%s%s", state->upper_dir, path);
    snprintf(lower_path, PATH_MAX, "%s%s", state->lower_dir, path);

    filler(buf, ".",  NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // remember what we added from upper so we don't duplicate
    char seen[512][NAME_MAX];
    int  seen_count = 0;

    // Pass 1: upper_dir
    // add real files, skip .wh.* marker files
    DIR *du = opendir(upper_path);
    if (du) {
        struct dirent *de;
        while ((de = readdir(du)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (strncmp(de->d_name, ".wh.", 4) == 0) continue;

            filler(buf, de->d_name, NULL, 0, 0);
            strncpy(seen[seen_count++], de->d_name, NAME_MAX);
        }
        closedir(du);
    }

    // Pass 2: lower_dir
    // skip whiteout'd files and files already added from upper
    DIR *dl = opendir(lower_path);
    if (dl) {
        struct dirent *de;
        while ((de = readdir(dl)) != NULL) {
            if (de->d_name[0] == '.') continue;

            // skip if whiteout exists
            if (is_whiteout(path, de->d_name)) continue;

            // skip if already added from upper
            int already_seen = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], de->d_name) == 0) {
                    already_seen = 1;
                    break;
                }
            }
            if (already_seen) continue;

            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dl);
    }

    return 0;
}

/* ─────────────────────────────────────────
   RMDIR: remove directory from upper only
───────────────────────────────────────── */
static int unionfs_rmdir(const char *path) {
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[PATH_MAX];
    snprintf(upper_path, PATH_MAX, "%s%s", state->upper_dir, path);

    if (rmdir(upper_path) == -1) return -errno;
    return 0;
}/* TEMPORARY GETATTR FOR TESTING */
static int unionfs_getattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi) {
    struct mini_unionfs_state *state = UNIONFS_DATA;

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    char whiteout_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", state->upper_dir, path);
    snprintf(lower_path, PATH_MAX, "%s%s", state->lower_dir, path);
    build_whiteout_path(path, whiteout_path);

    // root directory always exists
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // whiteout check first
    if (access(whiteout_path, F_OK) == 0)
        return -ENOENT;

    // check upper
    if (access(upper_path, F_OK) == 0)
        return lstat(upper_path, stbuf) == -1 ? -errno : 0;

    // check lower
    if (access(lower_path, F_OK) == 0)
        return lstat(lower_path, stbuf) == -1 ? -errno : 0;

    return -ENOENT;
}
/* TEMPORARY MAIN FOR TESTING - Person 1 will replace this on integration */
static struct fuse_operations unionfs_oper = {
    .unlink  = unionfs_unlink,
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .rmdir   = unionfs_rmdir,
};

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_dir>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    argv[1] = argv[3];
    argc    = 2;

    return fuse_main(argc, argv, &unionfs_oper, state);
}
