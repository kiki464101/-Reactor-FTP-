/**
 * @file handler.c
 * @brief Worker-thread command handlers for the Reactor+ThreadPool server
 *
 * Each handler runs in its own worker thread with EXCLUSIVE access to
 * sess->fd (the fd has been removed from the main epoll set before the
 * task was submitted).  The handler sends its response and, when done,
 * re-arms the fd via rearm_fd() so the main reactor can read the next
 * command.
 *
 * No forking.  No shared state between concurrent workers on different
 * sessions.  File I/O (stat, open, read, write) is done here where it
 * can block without starving the main event loop.
 */

#define _GNU_SOURCE
#include "handler.h"
#include "protocol.h"
#include "thread_pool.h"
#include "ipc_shm.h"
#include "sys_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>

/* ================================================================== */
/*  worker_func — thread pool entry point                              */
/* ================================================================== */
void *worker_func(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (1) {
        /* Pop task from queue (blocking) */
        pthread_mutex_lock(&pool->mutex);
        while (pool->count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->cond, &pool->mutex);
        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        task_t task = pool->queue[pool->head];
        pool->head = (pool->head + 1) % MAX_QUEUE;
        pool->count--;
        pthread_mutex_unlock(&pool->mutex);

        client_session_t *sess = (client_session_t *)task.session;
        if (!sess) { free(task.payload); continue; }

        /* Dispatch */
        switch (task.type) {
        case TASK_LOGIN:
            worker_handle_login(sess, task.payload, task.payload_len);
            break;
        case TASK_LS:
            worker_handle_ls(sess);
            break;
        case TASK_GET:
            worker_handle_get(sess, task.payload, task.payload_len);
            break;
        case TASK_PUT:
            worker_handle_put(sess, task.payload, task.payload_len);
            break;
        case TASK_BYE:
            worker_handle_bye(sess);
            break;
        }

        free(task.payload);

        /* Re-arm fd for next command (except BYE which closed it) */
        if (task.type != TASK_BYE && sess->fd >= 0) {
            struct epoll_event ev;
            ev.events  = EPOLLIN | EPOLLET;
            ev.data.ptr = sess;
            if (epoll_ctl(sess->epoll_fd, EPOLL_CTL_MOD, sess->fd, &ev) < 0)
                epoll_ctl(sess->epoll_fd, EPOLL_CTL_ADD, sess->fd, &ev);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Send a packet and return 0 on success, -1 on error. */
static int tx(client_session_t *sess, int cmd, int result,
              const unsigned char *data, int dlen)
{
    return send_packet(sess->fd, cmd, result, data, dlen);
}

/** Convenience: send OK with no data. */
static int ack_ok(client_session_t *sess, int cmd)
{
    return tx(sess, cmd, 1, NULL, 0);
}

/* ================================================================== */
/*  LOGIN                                                              */
/* ================================================================== */
void worker_handle_login(client_session_t *sess,
                         const unsigned char *payload, int plen)
{
    /* payload = [4B cmd_no] [4B user_len] [user] [4B pass_len] [pass] */
    if (plen < 12) { tx(sess, FTP_CMD_LOGIN, 0, NULL, 0); return; }

    int user_len = get_le32(payload + 4);
    if (user_len < 1 || user_len > 63 || 8 + user_len + 4 > plen) {
        tx(sess, FTP_CMD_LOGIN, 0, NULL, 0);
        return;
    }

    int pass_len = get_le32(payload + 8 + user_len);
    if (pass_len < 1 || pass_len > 63 ||
        8 + user_len + 4 + pass_len > plen) {
        tx(sess, FTP_CMD_LOGIN, 0, NULL, 0);
        return;
    }

    char username[64] = {0}, password[64] = {0};
    memcpy(username, payload + 8, (size_t)(user_len < 63 ? user_len : 63));
    memcpy(password, payload + 8 + 4 + user_len,
           (size_t)(pass_len < 63 ? pass_len : 63));

    if (verify_user(username, password) == 0) {
        char session_id[32];
        snprintf(session_id, sizeof(session_id), "SID-%d", sess->fd);
        tx(sess, FTP_CMD_LOGIN, 1,
           (unsigned char *)session_id, (int)strlen(session_id));
        /* shm uses fd as client id (no more fork/pids) */
        shm_add_client(sess->shm, sess->fd, sess->ip,
                       sess->port, "Online");
        sess->logged_in = true;
    } else {
        tx(sess, FTP_CMD_LOGIN, 0,
           (unsigned char *)"auth failed", 11);
    }
}

/* ================================================================== */
/*  LS – list shared directory                                         */
/* ================================================================== */
void worker_handle_ls(client_session_t *sess)
{
    shm_update_status(sess->shm, sess->fd, "Refreshing...");

    char  buf[SIZE] = {0};
    int   off = 0;

    DIR *dir = opendir(MY_FTP_BOOT);
    if (!dir) {
        tx(sess, FTP_CMD_LS, 0, (unsigned char *)"opendir fail", 12);
        shm_update_status(sess->shm, sess->fd, "Online");
        return;
    }

    struct dirent *d;
    while ((d = readdir(dir)) != NULL) {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
            continue;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off - 1,
                        "%s\n", d->d_name);
    }
    closedir(dir);

    tx(sess, FTP_CMD_LS, 1, (unsigned char *)buf, off);
    shm_update_status(sess->shm, sess->fd, "Online");
}

/* ================================================================== */
/*  GET – send file size ACK then stream raw content                   */
/* ================================================================== */
void worker_handle_get(client_session_t *sess,
                       const unsigned char *payload, int plen)
{
    /* payload = [4B cmd_no] [4B name_len] [filename…] */
    if (plen < 12) { tx(sess, FTP_CMD_GET, 0, NULL, 0); return; }

    int name_len = get_le32(payload + 4);
    if (name_len < 1 || 8 + name_len > plen) {
        tx(sess, FTP_CMD_GET, 0, NULL, 0);
        return;
    }

    char filename[256] = {0};
    int copy_len = (name_len < 255) ? name_len : 255;
    memcpy(filename, payload + 8, (size_t)copy_len);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);

    struct stat st;
    if (stat(path, &st) < 0) {
        tx(sess, FTP_CMD_GET, 0, (unsigned char *)"not found", 9);
        return;
    }

    /* Send ACK with filesize */
    unsigned char size_le[4];
    put_le32(size_le, (int)st.st_size);
    tx(sess, FTP_CMD_GET, 1, size_le, 4);

    shm_update_status(sess->shm, sess->fd, "Downloading 0%");

    /* Stream the file in chunks */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        shm_update_status(sess->shm, sess->fd, "Idle");
        return;
    }

    unsigned char buf[4096];
    int  sent     = 0;
    int  last_pct = -1;
    int  total    = (int)st.st_size;

    while (sent < total) {
        int r = (int)read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        /* send raw bytes — no protocol framing on data stream */
        int w = (int)write(sess->fd, buf, (size_t)r);
        if (w < 0) break;
        sent += w;

        /* Update shm status every ~10% */
        int pct = (total > 0) ? (sent * 100 / total) : 0;
        if (pct - last_pct >= 10) {
            last_pct = pct;
            char stbuf[64];
            snprintf(stbuf, sizeof(stbuf), "Downloading %d%%", pct);
            shm_update_status(sess->shm, sess->fd, stbuf);
        }
    }
    close(fd);
    shm_update_status(sess->shm, sess->fd, "Idle");

    /* Notify client that transfer is complete */
    unsigned char done[4];
    put_le32(done, sent);
    tx(sess, FTP_CMD_DONE, 1, done, 4);
}

/* ================================================================== */
/*  PUT – send ACK then receive file content                           */
/* ================================================================== */
void worker_handle_put(client_session_t *sess,
                       const unsigned char *payload, int plen)
{
    /* payload = [4B cmd_no] [4B name_len] [filename] [4B filesize] */
    if (plen < 16) { tx(sess, FTP_CMD_PUT, 0, NULL, 0); return; }

    int name_len = get_le32(payload + 4);
    if (name_len < 1 || 8 + name_len + 4 > plen) {
        tx(sess, FTP_CMD_PUT, 0, NULL, 0);
        return;
    }

    char filename[256] = {0};
    int copy_len = (name_len < 255) ? name_len : 255;
    memcpy(filename, payload + 8, (size_t)copy_len);

    int filesize = get_le32(payload + 8 + name_len);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        tx(sess, FTP_CMD_PUT, 0, (unsigned char *)"cannot create", 13);
        return;
    }

    /* Send ACK */
    ack_ok(sess, FTP_CMD_PUT);

    shm_update_status(sess->shm, sess->fd, "Uploading 0%");

    /* Receive file content in chunks */
    unsigned char buf[4096];
    int  received  = 0;
    int  last_pct  = -1;

    while (received < filesize) {
        int remaining = filesize - received;
        int chunk = (remaining < 4096) ? remaining : 4096;
        int r = (int)read(sess->fd, buf, (size_t)chunk);
        if (r <= 0) {
            if (errno == EINTR) continue;
            break;   /* client disconnected or error */
        }
        int w = (int)write(fd, buf, (size_t)r);
        if (w < 0) break;
        received += w;

        int pct = (filesize > 0) ? (received * 100 / filesize) : 0;
        if (pct - last_pct >= 10) {
            last_pct = pct;
            char stbuf[64];
            snprintf(stbuf, sizeof(stbuf), "Uploading %d%%", pct);
            shm_update_status(sess->shm, sess->fd, stbuf);
        }
    }
    close(fd);
    shm_update_status(sess->shm, sess->fd, "Idle");

    /* Notify client that transfer is complete */
    unsigned char done[4];
    put_le32(done, received);
    tx(sess, FTP_CMD_DONE, (received == filesize) ? 1 : 0, done, 4);
}

/* ================================================================== */
/*  BYE – cleanup and disconnect                                       */
/* ================================================================== */
void worker_handle_bye(client_session_t *sess)
{
    if (sess->logged_in) {
        shm_remove_client(sess->shm, sess->fd);
    }
    shutdown(sess->fd, SHUT_RDWR);
    close(sess->fd);
    /* Don't rearm — fd is gone.  Session is freed by the cleanup path
     * in main.c when EPOLLHUP is detected (or on next epoll_wait the
     * closed fd will trigger EPOLLERR and be cleaned up). */
    sess->fd = -1;
}
