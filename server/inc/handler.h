#ifndef SERVER_HANDLER_H
#define SERVER_HANDLER_H

#include "thread_pool.h"
#include "ipc_shm.h"
#include <sys/epoll.h>
#include <netinet/in.h>   /* struct sockaddr_in */

/* Client session — persists across tasks on the same fd */
typedef struct client_session {
    int                fd;
    struct sockaddr_in addr;
    client_info_t     *shm;
    int                epoll_fd;
    char               ip[32];
    int                port;
    bool               logged_in;
    /* upload state */
    bool               uploading;
    int                upload_fd;
    int                upload_total;
    int                upload_received;
    char               upload_filename[256];
} client_session_t;

/* Worker-thread command handlers (called from worker_func in handler.c) */

void worker_handle_login(client_session_t *sess,
                         const unsigned char *payload, int plen);

void worker_handle_ls(client_session_t *sess,
                     const unsigned char *payload, int plen);

void worker_handle_get(client_session_t *sess,
                       const unsigned char *payload, int plen);

void worker_handle_put(client_session_t *sess,
                       const unsigned char *payload, int plen);

void worker_handle_bye(client_session_t *sess);

void worker_handle_listdir(client_session_t *sess,
                           const unsigned char *payload, int plen);

#endif
