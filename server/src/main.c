/**
 * @file main.c
 * @brief FTP Server – Reactor (epoll) + Thread Pool architecture
 *
 * Single-process with:
 *   Main thread: epoll_wait → accept + command dispatch
 *   Worker pool: thread_pool.c → file I/O, protocol responses
 *   TUI thread:  1s refresh of shared-memory monitor table
 *
 * Replaces the old fork-per-client model.
 *
 * Build:  cd server && mkdir build && cd build && cmake .. && make
 * Run:    ./bin/ftp_server 0.0.0.0 8888
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#include "protocol.h"
#include "handler.h"
#include "ipc_shm.h"
#include "thread_pool.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */
#define MAX_EVENTS  1024
#define LISTEN_BACKLOG 128

/* ------------------------------------------------------------------ */
/*  Globals (shared across threads)                                    */
/* ------------------------------------------------------------------ */
static client_info_t  *g_shm      = NULL;
static thread_pool_t   g_pool;
static int             g_listen_fd = -1;
static int             g_epfd      = -1;
static volatile bool   g_running   = true;

/* ------------------------------------------------------------------ */
/*  Session table (maps fd → client_session_t*)                        */
/*  Simple array since MAX_EVENTS is known.                            */
/* ------------------------------------------------------------------ */
static client_session_t *g_sessions[MAX_EVENTS];   /* indexed by fd */
static int               g_next_client_id = 1;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */
static void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ------------------------------------------------------------------ */
/*  Socket initialisation (unchanged from original)                    */
/* ------------------------------------------------------------------ */
static int server_init(const char *ip, const char *port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sockfd); return -1;
    }
    if (listen(sockfd, LISTEN_BACKLOG) < 0) {
        perror("listen"); close(sockfd); return -1;
    }
    return sockfd;
}

/* ------------------------------------------------------------------ */
/*  Handle a new client connection                                     */
/* ------------------------------------------------------------------ */
static client_session_t *accept_client(void)
{
    struct sockaddr_in cli;
    socklen_t addrlen = sizeof(cli);
    int cfd = accept(g_listen_fd, (struct sockaddr *)&cli, &addrlen);
    if (cfd < 0) return NULL;

    set_nonblock(cfd);

    /* allocate session */
    client_session_t *sess = calloc(1, sizeof(client_session_t));
    if (!sess) { close(cfd); return NULL; }

    sess->fd       = cfd;
    sess->addr     = cli;
    sess->shm      = g_shm;
    sess->epoll_fd = g_epfd;
    strncpy(sess->ip, inet_ntoa(cli.sin_addr), sizeof(sess->ip) - 1);
    sess->port     = ntohs(cli.sin_port);

    /* register in session table */
    if (cfd < MAX_EVENTS)
        g_sessions[cfd] = sess;

    /* add to epoll (edge-triggered) */
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLET;
    ev.data.ptr = sess;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, cfd, &ev);

    printf("[server] new connection fd=%d from %s:%d\n", cfd, sess->ip, sess->port);
    return sess;
}

/* ------------------------------------------------------------------ */
/*  Dispatch a command packet from a client                            */
/* ------------------------------------------------------------------ */
static void dispatch_command(client_session_t *sess,
                             unsigned char *payload, int plen)
{
    if (plen < 4) { free(payload); return; }

    int cmd_no = get_le32(payload);
    task_t task;
    memset(&task, 0, sizeof(task));
    task.session     = sess;
    task.payload     = payload;     /* worker will free */
    task.payload_len = plen;

    switch (cmd_no) {
    case FTP_CMD_LOGIN:
        task.type = TASK_LOGIN;
        /* Remove fd from epoll — worker will rearm when done */
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
        thread_pool_submit(&g_pool, &task);
        break;

    case FTP_CMD_LS:
        task.type = TASK_LS;
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
        thread_pool_submit(&g_pool, &task);
        break;

    case FTP_CMD_GET:
        task.type = TASK_GET;
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
        thread_pool_submit(&g_pool, &task);
        break;

    case FTP_CMD_PUT:
        task.type = TASK_PUT;
        /* Worker will enter upload mode — remove from epoll to avoid
         * data race between reactor reads and worker reads. */
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
        thread_pool_submit(&g_pool, &task);
        break;

    case FTP_CMD_BYE:
        task.type = TASK_BYE;
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
        thread_pool_submit(&g_pool, &task);
        break;

    default:
        free(payload);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Read a command from a client fd (non-blocking, edge-triggered)    */
/* ------------------------------------------------------------------ */
static void handle_client_read(client_session_t *sess)
{
    /* Read all available complete packets */
    while (1) {
        int plen;
        unsigned char *payload = read_packet(sess->fd, &plen);
        if (!payload) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                /* No more data available right now — fine */
                break;
            }
            /* True error or disconnect */
            printf("[server] fd=%d disconnected (read error)\n", sess->fd);
            epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
            close(sess->fd);
            if (sess->fd < MAX_EVENTS) g_sessions[sess->fd] = NULL;
            free(sess);
            return;
        }
        dispatch_command(sess, payload, plen);
        /* After dispatch, the fd is removed from epoll.
         * Worker will rearm when done. Stop reading. */
        break;
    }
}

/* ================================================================== */
/*  TUI Thread (runs independently of the Reactor loop)               */
/* ================================================================== */
static void tui_refresh(client_info_t *shm)
{
    system("clear");

    printf("\033[1;36m");
    printf("============================================================\n");
    printf("  [ Embedded Remote File Manager - Server Monitor ]\n");
    printf("  Architecture: Reactor (epoll) + Thread Pool (%d workers)\n",
           g_pool.num_threads);
    printf("  Shared Dir: %s\n", MY_FTP_BOOT);
    printf("============================================================\033[0m\n");

    int active = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (shm[i].active) active++;

    printf(" [Online Clients: %d / %d]\n", active, MAX_CLIENTS);
    printf("------------------------------------------------------------\n");
    printf(" %-8s | %-15s | %-6s | %s\n", "CID", "Client IP", "Port", "Current State");
    printf("------------------------------------------------------------\n");

    if (active == 0) {
        printf(" (no connected clients)\n");
    } else {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!shm[i].active) continue;
            const char *color = "\033[0;32m";
            if (strstr(shm[i].status, "Download")) color = "\033[0;33m";
            else if (strstr(shm[i].status, "Upload")) color = "\033[0;31m";
            printf(" %-8d | %-15s | %-6d | %s%s\033[0m\n",
                   shm[i].pid, shm[i].ip, shm[i].port, color, shm[i].status);
        }
    }
    printf("------------------------------------------------------------\n");

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    printf(" [%02d:%02d:%02d] Server running. Press Ctrl+C to stop.\n",
           lt->tm_hour, lt->tm_min, lt->tm_sec);
    printf("============================================================\n");
}

static void *tui_thread_func(void *arg)
{
    client_info_t *shm = (client_info_t *)arg;
    while (g_running) {
        tui_refresh(shm);
        sleep(1);
    }
    return NULL;
}

/* ================================================================== */
/*  Signal handler                                                     */
/* ================================================================== */
static void sigint_handler(int sig)
{
    (void)sig;
    g_running = false;
    /* Wake epoll_wait by closing listen fd */
    if (g_listen_fd >= 0) close(g_listen_fd);
}

/* ================================================================== */
/*  Cleanup                                                            */
/* ================================================================== */
static void cleanup(void)
{
    thread_pool_destroy(&g_pool);

    /* Close all active client fds */
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (g_sessions[i]) {
            close(g_sessions[i]->fd);
            free(g_sessions[i]);
            g_sessions[i] = NULL;
        }
    }
    if (g_epfd >= 0) close(g_epfd);
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        fprintf(stderr, "  e.g.: %s 0.0.0.0 8888\n", argv[0]);
        return 1;
    }

    /* --- 1. init shared memory --- */
    g_shm = shm_init();
    if (!g_shm) {
        fprintf(stderr, "FATAL: shared memory init failed\n");
        return 1;
    }

    /* --- 2. init socket --- */
    g_listen_fd = server_init(argv[1], argv[2]);
    if (g_listen_fd < 0) return 1;
    set_nonblock(g_listen_fd);

    /* --- 3. init epoll --- */
    g_epfd = epoll_create1(0);
    if (g_epfd < 0) { perror("epoll_create1"); return 1; }

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.fd  = g_listen_fd;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_listen_fd, &ev);

    /* --- 4. init thread pool --- */
    if (thread_pool_init(&g_pool, 8) < 0) {
        fprintf(stderr, "FATAL: thread pool init failed\n");
        return 1;
    }

    /* --- 5. signal handler --- */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    /* --- 6. start TUI thread --- */
    pthread_t tui_tid;
    pthread_create(&tui_tid, NULL, tui_thread_func, g_shm);
    pthread_detach(tui_tid);

    printf("[server] Reactor+ThreadPool listening on %s:%s (8 workers, epoll)\n",
           argv[1], argv[2]);

    /* ================================================================ */
    /*  7. MAIN REACTOR LOOP                                            */
    /* ================================================================ */
    struct epoll_event events[MAX_EVENTS];

    while (g_running) {
        int n = epoll_wait(g_epfd, events, MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; i++) {
            /* Check if this is the listen fd */
            if (events[i].data.fd == g_listen_fd) {
                if (events[i].events & EPOLLIN) {
                    while (accept_client() != NULL)
                        ;  /* accept all pending connections */
                }
                continue;
            }

            /* Client fd event */
            client_session_t *sess = (client_session_t *)events[i].data.ptr;
            if (!sess) continue;

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                printf("[server] fd=%d hangup\n", sess->fd);
                epoll_ctl(g_epfd, EPOLL_CTL_DEL, sess->fd, NULL);
                if (sess->fd < MAX_EVENTS) g_sessions[sess->fd] = NULL;
                close(sess->fd);
                if (sess->logged_in)
                    shm_remove_client(g_shm, sess->fd);
                free(sess);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                handle_client_read(sess);
            }
        }
    }

    /* --- cleanup --- */
    cleanup();
    pthread_join(tui_tid, NULL);
    printf("[server] shutdown complete\n");
    return 0;
}
