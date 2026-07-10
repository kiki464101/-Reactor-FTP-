/**
 * @file handler.c
 * @brief Child-process handler with state machine
 *
 * Each child runs its own poll loop.  When idle (ST_IDLE) it reads a
 * protocol packet and dispatches.  During a PUT upload it enters
 * ST_UPLOADING where raw bytes are read directly without protocol
 * parsing, preventing the file content from being mistaken for a
 * 0xC0 packet header.
 */

#define _GNU_SOURCE
#include "handler.h"
#include "protocol.h"
#include "sys_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <dirent.h>

/* ------------------------------------------------------------------ */
/*  Forward helpers                                                   */
/* ------------------------------------------------------------------ */
static void handle_login(int confd, const unsigned char *payload,
                         int plen, const char *ip, int port,
                         client_info_t *shm);
static void handle_ls(int confd);
static void handle_get(int confd, const unsigned char *payload, int plen);
static void handle_put(int confd, const unsigned char *payload, int plen,
                       int *out_pid, client_info_t *shm);
static void handle_bye(int confd, client_info_t *shm);

/* ------------------------------------------------------------------ */
/*  handle_client – child process entry / main loop                   */
/* ------------------------------------------------------------------ */
void handle_client(int confd, const char *ip, int port, client_info_t *shm)
{
    int my_pid = getpid();
    handler_state_t state = ST_IDLE;

    /* --- upload state variables --- */
    int   ul_fd        = -1;
    int   ul_total     = 0;
    int   ul_received  = 0;
    char  ul_filename[256] = {0};

    struct pollfd pfd = { .fd = confd, .events = POLLIN };

    while (1) {
        int r = poll(&pfd, 1, 1000);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;   /* timeout */

        /* --- check peer disconnection --- */
        if (pfd.revents & (POLLRDHUP | POLLHUP | POLLERR)) break;
        if (!(pfd.revents & POLLIN)) continue;

        /* ========================================================== */
        /*  STATE: UPLOADING – read raw file data                     */
        /* ========================================================== */
        if (state == ST_UPLOADING) {
            int chunk = 4096;
            int remaining = ul_total - ul_received;
            if (remaining < chunk) chunk = remaining;
            if (chunk <= 0) {  /* done */
                close(ul_fd);
                ul_fd = -1;
                shm_update_status(shm, my_pid, "Idle");
                state = ST_IDLE;
                continue;
            }
            unsigned char buf[4096];
            int n = (int)read(confd, buf, (size_t)chunk);
            if (n <= 0) break;
            write(ul_fd, buf, (size_t)n);
            ul_received += n;
            continue;
        }

        /* ========================================================== */
        /*  STATE: IDLE – read one protocol packet                    */
        /* ========================================================== */
        int plen;
        unsigned char *payload = read_packet(confd, &plen);
        if (!payload) break;   /* disconnect or protocol error */

        if (plen < 4) { free(payload); continue; }
        int cmd_no = get_le32(payload);

        switch (cmd_no) {
        case FTP_CMD_LOGIN:
            handle_login(confd, payload, plen, ip, port, shm);
            break;
        case FTP_CMD_LS:
            handle_ls(confd);
            break;
        case FTP_CMD_GET:
            handle_get(confd, payload, plen);
            break;
        case FTP_CMD_PUT:
            handle_put(confd, payload, plen,
                       &ul_fd, shm);
            if (ul_fd >= 0) {
                state = ST_UPLOADING;
                /* filename and total should be set by handle_put */
            }
            break;
        case FTP_CMD_BYE:
            handle_bye(confd, shm);
            /* handle_bye closes socket and calls exit(0) */
            break;
        default:
            break;
        }
        free(payload);
    }

    /* --- cleanup on unexpected disconnect --- */
    if (ul_fd >= 0) close(ul_fd);
    shm_remove_client(shm, my_pid);
    shutdown(confd, SHUT_RDWR);
    close(confd);
}

/* ================================================================== */
/*  Command handlers                                                  */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  LOGIN                                                             */
/* ------------------------------------------------------------------ */
static void handle_login(int confd, const unsigned char *payload,
                         int plen, const char *ip, int port,
                         client_info_t *shm)
{
    /* payload = [4B cmd_no] [4B user_len] [user] [4B pass_len] [pass] */
    if (plen < 12) { send_packet(confd, FTP_CMD_LOGIN, 0, NULL, 0); return; }

    int  user_len  = get_le32(payload + 4);
    int  pass_len  = get_le32(payload + 8 + user_len);

    /* bounds check */
    if (user_len < 1 || pass_len < 1 ||
        8 + user_len + 4 + pass_len > plen) {
        send_packet(confd, FTP_CMD_LOGIN, 0, NULL, 0);
        return;
    }

    char username[64], password[64];
    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));
    memcpy(username, payload + 8, (size_t)(user_len < 63 ? user_len : 63));
    memcpy(password, payload + 8 + 4 + user_len,
           (size_t)(pass_len < 63 ? pass_len : 63));

    if (verify_user(username, password) == 0) {
        /* generate pseudo session ID */
        char session[32];
        snprintf(session, sizeof(session), "SID-%d", getpid());
        send_packet(confd, FTP_CMD_LOGIN, 1,
                    (unsigned char *)session, (int)strlen(session));
        /* add to shared memory */
        int my_pid = getpid();
        shm_add_client(shm, my_pid, ip, port, "Online");
    } else {
        send_packet(confd, FTP_CMD_LOGIN, 0,
                    (unsigned char *)"auth failed", 11);
    }
}

/* ------------------------------------------------------------------ */
/*  LS – list directory                                               */
/* ------------------------------------------------------------------ */
static void handle_ls(int confd)
{
    char filebuf[SIZE] = {0};
    int  off = 0;

    DIR *dir = opendir(MY_FTP_BOOT);
    if (!dir) {
        send_packet(confd, FTP_CMD_LS, 0, (unsigned char *)"opendir fail", 12);
        return;
    }

    struct dirent *d;
    while ((d = readdir(dir)) != NULL) {
        /* skip "." and ".." */
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
            continue;
        off += snprintf(filebuf + off, sizeof(filebuf) - (size_t)off - 1,
                        "%s\n", d->d_name);
    }
    closedir(dir);

    send_packet(confd, FTP_CMD_LS, 1,
                (unsigned char *)filebuf, off);
}

/* ------------------------------------------------------------------ */
/*  GET – send file size packet, then raw stream                     */
/* ------------------------------------------------------------------ */
static void handle_get(int confd, const unsigned char *payload, int plen)
{
    /* payload = [4B cmd_no] [4B arg_len] [filename…] */
    if (plen < 8) { send_packet(confd, FTP_CMD_GET, 0, NULL, 0); return; }

    int arg_len = get_le32(payload + 4);
    if (arg_len < 1 || 8 + arg_len > plen) {
        send_packet(confd, FTP_CMD_GET, 0, NULL, 0);
        return;
    }

    char filename[256];
    memcpy(filename, payload + 8, (size_t)(arg_len < 255 ? arg_len : 255));
    filename[arg_len < 255 ? arg_len : 255] = '\0';

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);

    struct stat st;
    if (stat(path, &st) < 0) {
        send_packet(confd, FTP_CMD_GET, 0,
                    (unsigned char *)"not found", 9);
        return;
    }

    /* send size as 4-byte LE in the response data field */
    unsigned char size_le[4];
    put_le32(size_le, (int)st.st_size);
    send_packet(confd, FTP_CMD_GET, 1, size_le, 4);

    /* send raw file content */
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    int my_pid = getpid();
    /* Note: shm pointer not passed to this static fn; we could update
       via a global if needed – for now status update is optional here */

    unsigned char buf[4096];
    int sent = 0;
    while (sent < st.st_size) {
        int r = (int)read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        write(confd, buf, (size_t)r);
        sent += r;
    }
    close(fd);
}

/* ------------------------------------------------------------------ */
/*  PUT – ack, then enter UPLOADING state                             */
/* ------------------------------------------------------------------ */
static void handle_put(int confd, const unsigned char *payload, int plen,
                       int *out_fd, client_info_t *shm)
{
    /* payload = [4B cmd_no] [4B arg_len] [filename…] */
    if (plen < 8) { send_packet(confd, FTP_CMD_PUT, 0, NULL, 0); return; }

    int arg_len = get_le32(payload + 4);
    if (arg_len < 1 || 8 + arg_len > plen) {
        send_packet(confd, FTP_CMD_PUT, 0, NULL, 0);
        return;
    }

    char filename[256];
    memcpy(filename, payload + 8, (size_t)(arg_len < 255 ? arg_len : 255));
    filename[arg_len < 255 ? arg_len : 255] = '\0';

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);

    /* open file for writing (create / truncate) */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        send_packet(confd, FTP_CMD_PUT, 0,
                    (unsigned char *)"cannot create", 13);
        *out_fd = -1;
        return;
    }

    /* send ACK */
    send_packet(confd, FTP_CMD_PUT, 1, NULL, 0);

    *out_fd = fd;
    /* The caller (handle_client) will set state = ST_UPLOADING.
       Upload size is unknown at this point; we rely on the client
       closing the connection or sending an EOF.  For production,
       the protocol could send filesize here. */
    int my_pid = getpid();
    shm_update_status(shm, my_pid, "Uploading");
}

/* ------------------------------------------------------------------ */
/*  BYE                                                                */
/* ------------------------------------------------------------------ */
static void handle_bye(int confd, client_info_t *shm)
{
    int my_pid = getpid();
    shm_remove_client(shm, my_pid);
    shutdown(confd, SHUT_RDWR);
    close(confd);
    exit(0);
}
