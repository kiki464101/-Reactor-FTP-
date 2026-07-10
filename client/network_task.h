/**
 * @file network_task.h
 * @brief Network task header for LVGL FTP client
 *
 * Declares the network thread, global socket state, and protocol
 * commands shared between UI and network threads.
 */

#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

#include <pthread.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Protocol command numbers (extending the existing protocol.h)      */
/* ------------------------------------------------------------------ */
#define FTP_CMD_LOGIN   1028   /* custom LOGIN command */

/* ------------------------------------------------------------------ */
/*  Global state shared between UI thread and network thread          */
/* ------------------------------------------------------------------ */
extern int          g_sockfd;
extern pthread_t    g_net_thread;
extern bool         g_network_running;
extern bool         g_login_ok;
extern char         g_session_info[64];

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

/**
 * Start connection + login on a background thread.
 * Returns immediately; success/failure delivered via async callback.
 */
bool network_start_connect(const char *ip, const char *port,
                           const char *username, const char *password);

/** Gracefully disconnect (send BYE, close socket, stop thread). */
void network_disconnect(void);

/** Send FTP_CMD_LS request. */
bool network_cmd_ls(void);

/** Send FTP_CMD_GET request for a single file. */
bool network_cmd_get(const char *filename);

/** Send FTP_CMD_PUT request to upload a file. */
bool network_cmd_put(const char *filename);

/** Cancel an ongoing transfer. */
void network_cancel_transfer(void);

/* ------------------------------------------------------------------ */
/*  Network thread entry point (do not call directly)                 */
/* ------------------------------------------------------------------ */
void *network_thread_func(void *arg);

#endif /* NETWORK_TASK_H */
