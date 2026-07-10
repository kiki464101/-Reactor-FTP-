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
static void handle_ls(int confd, client_info_t *shm);
static void handle_get(int confd, const unsigned char *payload, int plen,
                       client_info_t *shm);
static void handle_put(int confd, const unsigned char *payload, int plen,
                       int *out_fd, int *out_total, char *out_filename,
                       size_t filename_sz, client_info_t *shm);
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
    int   ul_last_pct  = -1;
    char  ul_filename[256] = {0};

    struct pollfd pfd = { .fd = confd, .events = POLLIN | POLLRDHUP };

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

            /* update shm status every ~10% */
            int pct = (ul_total > 0) ? (ul_received * 100 / ul_total) : 0;
            if (pct - ul_last_pct >= 10) {
                ul_last_pct = pct;
                char stbuf[64];
                snprintf(stbuf, sizeof(stbuf), "Uploading %d%%", pct);
                shm_update_status(shm, my_pid, stbuf);
            }
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
            handle_ls(confd, shm);
            break;
        case FTP_CMD_GET:
            handle_get(confd, payload, plen, shm);
            break;
        case FTP_CMD_PUT:
            handle_put(confd, payload, plen,
                       &ul_fd, &ul_total, ul_filename,
                       sizeof(ul_filename), shm);
            if (ul_fd >= 0) {
                ul_received = 0;
                ul_last_pct = -1;
                state = ST_UPLOADING;
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

    int user_len = get_le32(payload + 4);

    /* validate user_len BEFORE using it to read pass_len (prevents OOB) */
    if (user_len < 1 || user_len > 63 || 8 + user_len + 4 > plen) {
        send_packet(confd, FTP_CMD_LOGIN, 0, NULL, 0);
        return;
    }

    int pass_len = get_le32(payload + 8 + user_len);

    if (pass_len < 1 || pass_len > 63 ||
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
static void handle_ls(int confd, client_info_t *shm)
{
    int my_pid = getpid();
    shm_update_status(shm, my_pid, "Refreshing...");

    char filebuf[SIZE] = {0};
    int  off = 0;

    DIR *dir = opendir(MY_FTP_BOOT);
    if (!dir) {
        send_packet(confd, FTP_CMD_LS, 0, (unsigned char *)"opendir fail", 12);
        shm_update_status(shm, my_pid, "Online");
        return;
    }

    struct dirent *d;
    while ((d = readdir(dir)) != NULL) {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
            continue;
        off += snprintf(filebuf + off, sizeof(filebuf) - (size_t)off - 1,
                        "%s\n", d->d_name);
    }
    closedir(dir);

    send_packet(confd, FTP_CMD_LS, 1,
                (unsigned char *)filebuf, off);

    shm_update_status(shm, my_pid, "Online");
}

/* ------------------------------------------------------------------ */
/*  GET – send file size packet, then raw stream                     */
/* ------------------------------------------------------------------ */
static void handle_get(int confd, const unsigned char *payload, int plen,
                       client_info_t *shm)
{
    /* payload = [4B cmd_no] [4B arg_len] [4B name_len] [filename…]
     *   where arg_len = 4 + actual_name_len  (the 4 is the name_len prefix) */
    if (plen < 12) { send_packet(confd, FTP_CMD_GET, 0, NULL, 0); return; }

    int arg_len  = get_le32(payload + 4);
    int name_len = get_le32(payload + 8);   /* actual filename length */

    if (name_len < 1 || 12 + name_len > plen) {
        send_packet(confd, FTP_CMD_GET, 0, NULL, 0);
        return;
    }

    char filename[256];
    int copy_len = (name_len < 255) ? name_len : 255;
    memcpy(filename, payload + 12, (size_t)copy_len);
    filename[copy_len] = '\0';

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

    /* update shm status: "Downloading" */
    int my_pid = getpid();
    shm_update_status(shm, my_pid, "Downloading");

    /* send raw file content */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        shm_update_status(shm, my_pid, "Idle");
        return;
    }

    unsigned char buf[4096];
    int  sent       = 0;
    int  last_pct   = -1;
    int  total      = (int)st.st_size;

    while (sent < total) {
        int r = (int)read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        write(confd, buf, (size_t)r);
        sent += r;

        /* update shm status every ~10% to avoid thrashing */
        int pct = (total > 0) ? (sent * 100 / total) : 0;
        if (pct - last_pct >= 10) {
            last_pct = pct;
            char stbuf[64];
            snprintf(stbuf, sizeof(stbuf), "Downloading %d%%", pct);
            shm_update_status(shm, my_pid, stbuf);
        }
    }
    close(fd);

    /* restore status to Idle after transfer */
    shm_update_status(shm, my_pid, "Idle");
}

/* ------------------------------------------------------------------ */
/*  PUT – ack, then enter UPLOADING state                             */
/*  Protocol payload:                                                 */
/*    [4B cmd_no] [4B arg_len] [4B name_len] [filename] [4B filesize] */
/* ------------------------------------------------------------------ */
static void handle_put(int confd, const unsigned char *payload, int plen,
                       int *out_fd, int *out_total, char *out_filename,
                       size_t filename_sz, client_info_t *shm)
{
    /* minimum: cmd_no(4) + arg_len(4) + name_len(4) + filesize(4) = 16 */
    if (plen < 16) { send_packet(confd, FTP_CMD_PUT, 0, NULL, 0); return; }

    int arg_len  = get_le32(payload + 4);
    int name_len = get_le32(payload + 8);

    /* arg_len must cover name_len(4B prefix) + name + filesize(4B) */
    if (name_len < 1 || 8 + 4 + name_len + 4 > plen) {
        send_packet(confd, FTP_CMD_PUT, 0, NULL, 0);
        return;
    }

    /* extract filename */
    char filename[256];
    int copy_len = (name_len < 255) ? name_len : 255;
    memcpy(filename, payload + 12, (size_t)copy_len);
    filename[copy_len] = '\0';

    /* extract filesize (4-byte LE after filename) */
    int filesize = get_le32(payload + 12 + name_len);

    /* build output path */
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

    /* output upload metadata to caller */
    *out_fd = fd;
    *out_total = filesize;
    if (out_filename && filename_sz > 0) {
        strncpy(out_filename, filename, filename_sz - 1);
        out_filename[filename_sz - 1] = '\0';
    }

    /* update shared memory */
    int my_pid = getpid();
    shm_update_status(shm, my_pid, "Uploading");

    printf("[%d] Receiving upload: %s (%d bytes)\n", my_pid, filename, filesize);
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
