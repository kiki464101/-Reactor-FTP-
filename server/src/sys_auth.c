#include "sys_auth.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>   /* flock */

/* Try current directory first; fall back to ../server/ when run from bin/ */
#define AUTH_FILE_CWD  "./users.conf"
#define AUTH_FILE_ALT  "../server/users.conf"
#define LINE_MAX 256

int verify_user(const char *username, const char *password)
{
    int fd = open(AUTH_FILE_CWD, O_RDONLY);
    if (fd < 0) {
        fd = open(AUTH_FILE_ALT, O_RDONLY);
    }
    if (fd < 0) { perror("open users.conf"); return -1; }

    /* shared lock – multiple children can read concurrently */
    flock(fd, LOCK_SH);

    char line[LINE_MAX];
    int  found = -1;
    while (read(fd, line, 1) == 1) {
        /* read one line */
        int off = 0;
        while (off < LINE_MAX - 1 && line[off] != '\n') {
            if (read(fd, &line[++off], 1) <= 0) break;
        }
        /* trim trailing newline / carriage return */
        while (off >= 0 && (line[off] == '\n' || line[off] == '\r'))
            line[off--] = '\0';
        line[off + 1] = '\0';
        if (off <= 0) continue;           /* skip empty */

        /* lines are "username:password" */
        char saved[LINE_MAX];
        strncpy(saved, line, LINE_MAX - 1);
        saved[LINE_MAX - 1] = '\0';

        char *sep = strchr(saved, ':');
        if (!sep) continue;
        *sep = '\0';

        if (strcmp(saved, username) == 0 && strcmp(sep + 1, password) == 0) {
            found = 0;
            break;
        }
    }

    flock(fd, LOCK_UN);
    close(fd);
    return found;
}
