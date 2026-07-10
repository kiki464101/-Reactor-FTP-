/**
 * @file main.c
 * @brief Embedded Remote File Manager – Server Entry Point
 *
 * Architecture:
 *   Parent process: poll listen_fd, fork children, 1s TUI refresh.
 *   Child process:  handle_client() in handler.c with state machine.
 *   IPC:            shared-memory segment for the online client table.
 *
 * Build:
 *   mkdir build && cd build && cmake .. && make
 * Run:
 *   ./bin/ftp_server 0.0.0.0 8888
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
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"
#include "handler.h"
#include "ipc_shm.h"

/* ------------------------------------------------------------------ */
/*  Globals                                                           */
/* ------------------------------------------------------------------ */
static client_info_t *g_shm = NULL;

/* ------------------------------------------------------------------ */
/*  SIGCHLD handler – non-blocking reaper                             */
/* ------------------------------------------------------------------ */
static void sigchld_handler(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* reap one child per iteration */
    }
}

/* ------------------------------------------------------------------ */
/*  Socket initialisation                                             */
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
    if (listen(sockfd, 8) < 0) {
        perror("listen"); close(sockfd); return -1;
    }
    return sockfd;
}

/* ------------------------------------------------------------------ */
/*  TUI – print the monitoring table                                  */
/* ------------------------------------------------------------------ */
static void tui_refresh(client_info_t *shm)
{
    system("clear");

    /* Header */
    printf("\033[1;36m");   /* cyan bold */
    printf("============================================================\n");
    printf("  [ Embedded Remote File Manager - Server Monitor ]\n");
    printf("  %-15s %s\n", "Server:", "0.0.0.0  |  Port: 8888  |  Status: Running");
    printf("  %-15s %s\n", "Shared Dir:", MY_FTP_BOOT);
    printf("============================================================\033[0m\n");

    /* count active clients */
    int active = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm[i].active) active++;
    }
    printf(" [Online Clients: %d / %d]\n", active, MAX_CLIENTS);
    printf("------------------------------------------------------------\n");
    printf(" %-8s | %-15s | %-6s | %s\n", "PID", "Client IP", "Port", "Current State");
    printf("------------------------------------------------------------\n");

    if (active == 0) {
        printf(" (no connected clients)\n");
    } else {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!shm[i].active) continue;
            /* colour the status text */
            const char *color = "\033[0;32m";   /* green = Idle */
            if (strstr(shm[i].status, "Download")) color = "\033[0;33m";  /* yellow */
            else if (strstr(shm[i].status, "Upload")) color = "\033[0;31m"; /* red */

            printf(" %-8d | %-15s | %-6d | %s%s\033[0m\n",
                   shm[i].pid, shm[i].ip, shm[i].port, color, shm[i].status);
        }
    }
    printf("------------------------------------------------------------\n");

    /* Footer / hint */
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    printf(" [%02d:%02d:%02d] Server running. Press Ctrl+C to stop.\n",
           lt->tm_hour, lt->tm_min, lt->tm_sec);
    printf("============================================================\n");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        fprintf(stderr, "  e.g.: %s 0.0.0.0 8888\n", argv[0]);
        return 1;
    }

    /* --- 1. shared memory --- */
    g_shm = shm_init();
    if (!g_shm) {
        fprintf(stderr, "FATAL: shared memory init failed\n");
        return 1;
    }

    /* --- 2. SIGCHLD --- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* --- 3. socket --- */
    int listen_fd = server_init(argv[1], argv[2]);
    if (listen_fd < 0) return 1;

    /* --- 4. main loop (poll + TUI) --- */
    struct pollfd fds[1] = {{ .fd = listen_fd, .events = POLLIN }};
    int tui_counter = 0;

    while (1) {
        int r = poll(fds, 1, 1000);   /* 1s timeout */
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* refresh TUI every second */
        tui_refresh(g_shm);

        if (r > 0 && (fds[0].revents & POLLIN)) {
            struct sockaddr_in cli;
            socklen_t addrlen = sizeof(cli);
            int confd = accept(listen_fd, (struct sockaddr *)&cli, &addrlen);
            if (confd < 0) continue;

            char *cip = inet_ntoa(cli.sin_addr);
            int   cpt = ntohs(cli.sin_port);

            pid_t pid = fork();
            if (pid == 0) {
                /* child: close listen_fd, handle this client */
                close(listen_fd);
                handle_client(confd, cip, cpt, g_shm);
                _exit(0);
            } else if (pid > 0) {
                /* parent: close confd, continue polling */
                close(confd);
            } else {
                perror("fork");
            }
        }
    }

    close(listen_fd);
    return 0;
}
