/**
 * @file network_task.h
 * @brief Network task header for LVGL FTP client
 *
 * Declares the network thread, global socket state, protocol commands,
 * transfer queue, and thread pool for multi-file concurrent transfers.
 */

#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

#include <pthread.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Protocol command numbers                                          */
/* ------------------------------------------------------------------ */
#define FTP_CMD_LOGIN   1028

/* ------------------------------------------------------------------ */
/*  Transfer pool constants                                           */
/* ------------------------------------------------------------------ */
#define TRANSFER_POOL_SIZE  3
#define MAX_QUEUE_SIZE      256
#define MAX_SELECTED_FILES  128

/* ------------------------------------------------------------------ */
/*  Transfer task                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    char filename[256];
    bool is_upload;
} transfer_task_t;

/* ------------------------------------------------------------------ */
/*  Thread-safe transfer queue                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    transfer_task_t tasks[MAX_QUEUE_SIZE];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            cancelled;
} transfer_queue_t;

/* ------------------------------------------------------------------ */
/*  Transfer worker thread state                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    pthread_t thread;
    int       id;
    bool      running;
} transfer_worker_t;

/* ------------------------------------------------------------------ */
/*  Global state                                                      */
/* ------------------------------------------------------------------ */
extern int          g_sockfd;
extern pthread_t    g_net_thread;
extern bool         g_network_running;
extern bool         g_login_ok;
extern char         g_session_info[64];

/* Saved login credentials (for transfer workers to re-login) */
extern char g_login_ip[64];
extern char g_login_port[16];
extern char g_login_user[64];
extern char g_login_pass[64];

/* Transfer queue and thread pool */
extern transfer_queue_t  g_tx_queue;
extern transfer_worker_t g_tx_workers[TRANSFER_POOL_SIZE];
extern volatile int      g_active_transfers;

/* ------------------------------------------------------------------ */
/*  Transfer progress (thread-safe, written by net thread, read by UI) */
/* ------------------------------------------------------------------ */
typedef struct {
    int     percent;
    int     current_bytes;
    int     total_bytes;
    char    filename[256];
    bool    active;
    bool    is_upload;
} transfer_progress_t;

extern transfer_progress_t g_transfer_progress;

/* ------------------------------------------------------------------ */
/*  Public API (all called from UI thread)                            */
/* ------------------------------------------------------------------ */

/* ---- Connection ---- */
bool network_start_connect(const char *ip, const char *port,
                           const char *username, const char *password);
void network_disconnect(void);

/* ---- Single commands (main thread) ---- */
bool network_cmd_ls(const char *path);

/* ---- Single-file compatibility wrappers ---- */
bool network_cmd_get(const char *filename);
bool network_cmd_put(const char *filename);

/* ---- Multi-file transfer (queues tasks to thread pool) ---- */
bool network_cmd_get_multi(const char **filenames, int count);
bool network_cmd_put_multi(const char **filenames, int count);

/* ---- Cancel all transfers ---- */
void network_cancel_transfer(void);

/* ---- Transfer pool lifecycle ---- */
void transfer_pool_init(void);
void transfer_pool_stop(void);

/* ---- Network thread entry point (do not call directly) ---- */
void *network_thread_func(void *arg);

#endif /* NETWORK_TASK_H */
