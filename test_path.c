#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

char *upper_dir = "./upper";
char *lower_dir = "./lower";

char* resolve_path(const char *path) {
    static char full_path[PATH_MAX];
    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    char whiteout_path[PATH_MAX];

    snprintf(upper_path, PATH_MAX, "%s%s", upper_dir, path);
    snprintf(lower_path, PATH_MAX, "%s%s", lower_dir, path);
    snprintf(whiteout_path, PATH_MAX, "%s/.wh.%s", upper_dir, path + 1);

    if (access(whiteout_path, F_OK) == 0) {
        return NULL;
    }

    if (access(upper_path, F_OK) == 0) {
        strcpy(full_path, upper_path);
        return full_path;
    }

    if (access(lower_path, F_OK) == 0) {
        strcpy(full_path, lower_path);
        return full_path;
    }

    return NULL;
}

/*
int main() {
    char input[100];

    printf("Enter path: ");
    scanf("%s", input);

    char *res = resolve_path(input);

    if (res == NULL)
        printf("File not found\n");
    else
        printf("Found at: %s\n", res);

    return 0;
}
*/
