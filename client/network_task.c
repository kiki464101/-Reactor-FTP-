#define _GNU_SOURCE
/**
 * @file network_task.c
 * @brief Network thread for LVGL FTP client
 *
 * Runs in a dedicated pthread.  Receives raw TCP data, parses the
 * application-layer protocol (0xC0 header/trailer, little-endian
 * lengths), and pushes parsed results to the UI thread via
 * lv_async_call().
 *
 * Protocol format (request / response):
 *   0xC0 + pkg_len(4B LE) + cmd_no(4B LE) + [args...] + 0xC0
 *
 * Download flow:
 *   UI send GET  -> net reads response (filesize) -> enters DOWNLOAD state
 *   -> reads raw bytes from socket, writes to local file
 *   -> periodic lv_async_call() to update the progress bar
 *
 * Upload flow:
 *   UI send PUT  -> net reads ack from server -> enters UPLOAD state
 *   -> reads local file, writes raw bytes to socket
 *   -> periodic lv_async_call() to update the progress bar
 */

#include "network_task.h"
#include "ui_manager.h"

#include "protocol.h"
#include "../lvgl/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------------------------------------------------------ */
/*  Global definitions                                                */
/* ------------------------------------------------------------------ */
int          g_sockfd           = -1;
pthread_t    g_net_thread       = 0;
bool         g_network_running  = false;
bool         g_login_ok         = false;
char         g_session_info[64] = {0};

transfer_progress_t g_transfer_progress = {0};

/* Saved login credentials for transfer workers */
char g_login_ip[64]   = {0};
char g_login_port[16] = {0};
char g_login_user[64] = {0};
char g_login_pass[64] = {0};

/* Transfer queue and thread pool */
transfer_queue_t  g_tx_queue;
transfer_worker_t g_tx_workers[TRANSFER_POOL_SIZE];
volatile int      g_active_transfers = 0;

/* ------------------------------------------------------------------ */
/*  Internal download state                                           */
/* ------------------------------------------------------------------ */
#define CHUNK_SIZE 4096

typedef enum {
    ST_IDLE,
    ST_LOGIN_SENT,
    ST_WAIT_LS_RESP,
    ST_WAIT_GET_RESP,
    ST_DOWNLOADING,
    ST_WAIT_PUT_RESP,
    ST_UPLOADING,
} net_state_t;

static net_state_t  g_state            = ST_IDLE;
static int          g_dl_fd            = -1;     /* local file fd during download */
static int          g_dl_total         = 0;      /* total bytes expected */
static int          g_dl_received      = 0;      /* bytes received so far */
static char         g_dl_filename[256] = {0};
static int          g_last_progress    = -1;     /* avoid flooding async calls */

static int          g_ul_fd            = -1;
static int          g_ul_total         = 0;
static int          g_ul_sent          = 0;
static char         g_ul_filename[256] = {0};

/* ------------------------------------------------------------------ */
/*  Low-level helpers: read exactly N bytes (blocking)               */
/* ------------------------------------------------------------------ */
static int read_n(int fd, unsigned char *buf, int n)
{
    int offset = 0;
    while (offset < n) {
        int r = read(fd, buf + offset, (size_t)(n - offset));
        if (r <= 0) {
            if (r == 0) return offset;               /* EOF */
            if (errno == EINTR) continue;
            return -1;                                /* error */
        }
        offset += r;
    }
    return offset;
}

/* ------------------------------------------------------------------ */
/*  Packet-level helpers                                              */
/* ------------------------------------------------------------------ */

/** Read one complete protocol packet from the socket.
 *  Returns a malloc'd buffer (caller frees) or NULL on error/disconnect.
 *  On success *out_len gets the payload size (everything between header
 *  and trailer). */
static unsigned char *read_packet(int fd, int *out_len)
{
    unsigned char ch;
    /* ----- find 0xC0 header ----- */
    while (1) {
        int r = read(fd, &ch, 1);
        if (r <= 0) return NULL;
        if (ch == 0xC0) break;
    }
    /* consume duplicate 0xC0 bytes (trailer of previous packet) */
    while (1) {
        int r = read(fd, &ch, 1);
        if (r <= 0) return NULL;
        if (ch != 0xC0) break;
    }
    /* ch is now the first byte of pkg_len (little-endian) */
    int pkg_len = ch;
    int i;
    for (i = 1; i < 4; i++) {
        if (read(fd, &ch, 1) <= 0) return NULL;
        pkg_len |= (ch << (8 * i));
    }
    if (pkg_len < 10) return NULL;   /* sanity check */
    /* read the remaining payload (everything up to the trailer byte) */
    int payload_len = pkg_len - 6;    /* 1 header + 4 len + 1 trailer */
    if (payload_len <= 0) return NULL;
    unsigned char *payload = (unsigned char *)malloc((size_t)payload_len);
    if (!payload) return NULL;
    /* Use read_n to read exactly payload_len bytes — avoids false
     * termination when payload itself contains 0xC0 bytes. */
    if (read_n(fd, payload, payload_len) != payload_len) {
        free(payload);
        return NULL;
    }
    /* read and verify the 0xC0 trailer */
    if (read(fd, &ch, 1) <= 0 || ch != 0xC0) {
        free(payload);
        return NULL;
    }
    *out_len = payload_len;
    return payload;
}

/** Helper: put a 32-bit little-endian value into buf[0..3]. */
static inline void put_le32(unsigned char *buf, int val)
{
    buf[0] = (unsigned char)( val        & 0xFF);
    buf[1] = (unsigned char)((val >> 8)  & 0xFF);
    buf[2] = (unsigned char)((val >> 16) & 0xFF);
    buf[3] = (unsigned char)((val >> 24) & 0xFF);
}

/** Helper: read a 32-bit little-endian value from buf. */
static inline int get_le32(const unsigned char *buf)
{
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

/* ------------------------------------------------------------------ */
/*  Build a command packet (malloc'd, caller frees)                   */
/* ------------------------------------------------------------------ */
static unsigned char *build_cmd(int cmd_no,
                                const unsigned char *arg_data, int arg_len,
                                int *out_total_len)
{
    int pkg_len = 10 + arg_len;                /* header+len+cmd+trailer */
    unsigned char *pkt = (unsigned char *)malloc((size_t)pkg_len);
    if (!pkt) return NULL;
    int i = 0;
    pkt[i++] = 0xC0;                           /* header */
    put_le32(pkt + i, pkg_len);  i += 4;       /* pkg_len */
    put_le32(pkt + i, cmd_no);   i += 4;       /* cmd_no */
    if (arg_data && arg_len > 0) {
        memcpy(pkt + i, arg_data, (size_t)arg_len);
        i += arg_len;
    }
    pkt[i++] = 0xC0;                           /* trailer */
    *out_total_len = pkg_len;
    return pkt;
}

/** Build a GET/PUT command with a single string argument. */
static unsigned char *build_cmd_with_str(int cmd_no, const char *str,
                                          int *out_len)
{
    int slen = (int)strlen(str);
    int arg_len = 4 + slen;                    /* arg_len field + data */
    unsigned char *arg = (unsigned char *)malloc((size_t)arg_len);
    if (!arg) return NULL;
    put_le32(arg, slen);
    memcpy(arg + 4, str, (size_t)slen);
    unsigned char *pkt = build_cmd(cmd_no, arg, arg_len, out_len);
    free(arg);
    return pkt;
}

/** Build a PUT command with filename AND filesize.
 *  Protocol: [4B cmd_no] [4B arg_len] [4B name_len] [filename] [4B filesize]
 */
static unsigned char *build_cmd_put(const char *filename, int filesize,
                                     int *out_len)
{
    int slen = (int)strlen(filename);
    int arg_len = 4 + slen + 4;               /* name_len(4) + name + filesize(4) */
    unsigned char *arg = (unsigned char *)malloc((size_t)arg_len);
    if (!arg) return NULL;
    put_le32(arg,      slen);                  /* name_len prefix */
    memcpy(arg + 4, filename, (size_t)slen);   /* filename data */
    put_le32(arg + 4 + slen, filesize);        /* filesize (4-byte LE) */
    unsigned char *pkt = build_cmd(FTP_CMD_PUT, arg, arg_len, out_len);
    free(arg);
    return pkt;
}

/* ------------------------------------------------------------------ */
/*  Parse a response packet                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    int  cmd_no;
    int  res_result;      /* 0 = fail, 1 = success */
    int  res_len;
    const unsigned char *res_data;  /* pointer inside payload */
} resp_t;

static bool parse_response(const unsigned char *payload, int payload_len,
                            resp_t *rsp)
{
    if (payload_len < 9) return false;
    rsp->cmd_no     = get_le32(payload + 0);
    rsp->res_len    = get_le32(payload + 4);
    rsp->res_result = payload[8];
    rsp->res_data   = payload + 9;
    return true;
}

/* ================================================================== */
/*  Transfer queue operations (thread-safe)                           */
/* ================================================================== */

static void tx_queue_init(transfer_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void tx_queue_destroy(transfer_queue_t *q)
{
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static bool tx_queue_push(transfer_queue_t *q, const transfer_task_t *task)
{
    pthread_mutex_lock(&q->mutex);
    if (q->count >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }
    q->tasks[q->tail] = *task;
    q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static bool tx_queue_pop(transfer_queue_t *q, transfer_task_t *task)
{
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->cancelled) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    if (q->cancelled && q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }
    *task = q->tasks[q->head];
    q->head = (q->head + 1) % MAX_QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static void tx_queue_cancel_all(transfer_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    q->cancelled = true;
    pthread_cond_broadcast(&q->cond);
    q->head = q->tail = q->count = 0;
    pthread_mutex_unlock(&q->mutex);
}

static void tx_queue_reset(transfer_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    q->cancelled = false;
    pthread_mutex_unlock(&q->mutex);
}

/* ------------------------------------------------------------------ */
/*  Async-callback wrappers sent to UI thread                         */
/* ------------------------------------------------------------------ */
typedef struct {
    char text[512];
} str_data_t;

static str_data_t *make_str_data(const char *s)
{
    str_data_t *d = (str_data_t *)malloc(sizeof(str_data_t));
    if (d) { strncpy(d->text, s, sizeof(d->text) - 1); d->text[sizeof(d->text)-1] = '\0'; }
    return d;
}

static void cb_login_result(void *data)
{
    str_data_t *d = (str_data_t *)data;
    if (g_login_ok) {
        ui_switch_to_main();
    } else {
        /* ui_show_error covers both screens and re-enables login button
         * when we're still on the login screen (ui_set_status only
         * targets main_status_bar which doesn't exist yet). */
        ui_show_error(d ? d->text : "Login failed");
    }
    free(d);
}

static void cb_error(void *data)
{
    str_data_t *d = (str_data_t *)data;
    ui_show_error(d ? d->text : "Unknown error");
    free(d);
}

static void cb_disconnected(void *data)
{
    (void)data;
    g_login_ok = false;
    ui_switch_to_login();
}

static void cb_dl_progress(void *data)
{
    transfer_progress_t *p = (transfer_progress_t *)data;
    ui_update_progress(p->percent, p->current_bytes, p->total_bytes,
                       p->filename, p->is_upload);
    /* also feed batch panel progress bars */
    ui_update_transfer_progress(p->filename, p->percent,
                                 p->current_bytes, p->total_bytes,
                                 p->is_upload);
    free(p);
}

static void cb_dl_done(void *data)
{
    str_data_t *d = (str_data_t *)data;
    ui_hide_progress();
    ui_set_status(d ? d->text : "Download complete");
    ui_restore_status_after_delay();
    free(d);
}

typedef struct {
    char filename[256];
    bool is_upload;
} show_progress_data_t;

static void cb_show_progress(void *data)
{
    /* Popup is already created synchronously by the UI button handlers.
     * Network thread only drives data transfer; UI owns all widgets. */
    free(data);
}

static void cb_ul_done(void *data)
{
    str_data_t *d = (str_data_t *)data;
    ui_hide_progress();
    ui_set_status(d ? d->text : "Uploaded");
    ui_restore_status_after_delay();
    free(d);
}

/* ---- Multi-file batch state ---- */
static int           g_batch_total  = 0;   /* total files in batch */
static int           g_batch_done   = 0;   /* files completed */
static bool          g_batch_active = false;
static volatile bool g_transfer_cancelled = false;

typedef struct {
    char filename[256];
    bool success;
    bool is_upload;
} tx_done_data_t;

static void cb_tx_done(void *data)
{
    tx_done_data_t *d = (tx_done_data_t *)data;
    ui_on_transfer_done(d->filename, d->success, d->is_upload);
    free(d);
}

static void cb_show_error_popup(void *data)
{
    str_data_t *d = (str_data_t *)data;
    if (d) { ui_show_error_popup(d->text); free(d); }
}

static void normalize_local_path(const char *filename, char *path, size_t path_sz)
{
    const char *src = filename;
    if (!src || !path || path_sz == 0) return;
    if (strncmp(src, "./client/", 9) == 0) src += 9;
    else if (strncmp(src, "client/", 7) == 0) src += 7;
    else if (strncmp(src, "./", 2) == 0) src += 2;

    snprintf(path, path_sz, "./client/%s", src[0] ? src : "");
}

/* Start the next queued transfer. Called from main loop when IDLE.
 * Returns true if a transfer was started successfully. */
static bool batch_start_next(void)
{
    if (g_tx_queue.count == 0)
        return false;

    transfer_task_t task;
    if (!tx_queue_pop(&g_tx_queue, &task))
        return false;

    bool ok;
    if (task.is_upload)
        ok = network_cmd_put(task.filename);
    else
        ok = network_cmd_get(task.filename);

    if (!ok) {
        /* transfer failed to start -- mark as failed and continue */
        printf("[DEBUG] batch_start_next: failed to start '%s', skipping\n", task.filename);
        g_batch_done++;
        /* immediately try next in queue */
        return batch_start_next();
    }
    return true;
}

/* Check if all batch transfers are complete */
static void batch_check_complete(void)
{
    if (g_batch_active && g_tx_queue.count == 0 && g_state == ST_IDLE) {
        g_batch_active = false;
        str_data_t *msg = make_str_data("Transfer complete");
        if (msg) lv_async_call(cb_dl_done, msg);
    }
}

/* ------------------------------------------------------------------ */
/*  Download state management (called from network thread)            */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Download state management (called from network thread)            */
/* ------------------------------------------------------------------ */
static void start_download(const char *filename, int filesize)
{
    strncpy(g_dl_filename, filename, sizeof(g_dl_filename) - 1);
    g_dl_total    = filesize;
    g_dl_received = 0;
    g_last_progress = -1;
    g_active_transfers++;

    /* create / truncate local file in client/load/ */
    mkdir("./client/load", 0755);
    char dl_path[520];
    snprintf(dl_path, sizeof(dl_path), "./client/load/%s", filename);
    g_dl_fd = open(dl_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (g_dl_fd < 0) {
        str_data_t *err = make_str_data("Failed to create local file");
        if (err) lv_async_call(cb_error, err);
        g_state = ST_IDLE;
        return;
    }

    g_transfer_progress.active  = true;
    g_transfer_progress.is_upload = false;
    g_transfer_progress.percent = 0;
    g_transfer_progress.current_bytes = 0;
    g_transfer_progress.total_bytes = filesize;
    strncpy(g_transfer_progress.filename, filename,
            sizeof(g_transfer_progress.filename) - 1);

    g_state = ST_DOWNLOADING;

    /* NOTE: progress popup is created synchronously by UI button handlers
     * via ui_show_progress_batch(). Network layer only drives data transfer. */
}

static void update_dl_progress(void)
{
    int pct = (g_dl_total > 0) ? (g_dl_received * 100 / g_dl_total) : 0;
    if (pct - g_last_progress < 2 && pct < 100) return;
    g_last_progress = pct;

    transfer_progress_t *tp = (transfer_progress_t *)malloc(sizeof(transfer_progress_t));
    if (!tp) return;
    tp->percent       = pct;
    tp->current_bytes = g_dl_received;
    tp->total_bytes   = g_dl_total;
    tp->active        = true;
    tp->is_upload     = false;
    strncpy(tp->filename, g_dl_filename, sizeof(tp->filename) - 1);
    lv_async_call(cb_dl_progress, tp);
}

static void finish_download(bool success)
{
    if (g_dl_fd >= 0) { close(g_dl_fd); g_dl_fd = -1; }
    g_dl_received = 0;
    g_dl_total    = 0;
    g_last_progress = -1;
    g_transfer_progress.active = false;
    g_state = ST_IDLE;
    g_transfer_cancelled = false;
    if (g_active_transfers > 0) g_active_transfers--;

    printf("[DEBUG] finish_download: %s success=%d batch=%d done=%d/%d queue=%d\n",
           g_dl_filename, success, g_batch_active, g_batch_done, g_batch_total, g_tx_queue.count);

    /* per-file report to batch panel */
    {
        tx_done_data_t *d = (tx_done_data_t *)malloc(sizeof(*d));
        if (d) {
            strncpy(d->filename, g_dl_filename, sizeof(d->filename) - 1);
            d->filename[sizeof(d->filename) - 1] = '\0';
            d->success   = success;
            d->is_upload = false;
            lv_async_call(cb_tx_done, d);
        }
    }

    if (g_batch_active) {
        g_batch_done++;
        /* don't show "Downloaded" yet -- batch_check_complete handles it */
    } else {
        str_data_t *msg = make_str_data(success
            ? "Downloaded"
            : "Download failed");
        if (msg) lv_async_call(cb_dl_done, msg);
    }
}

/* ------------------------------------------------------------------ */
/*  Upload state management (called from network thread)              */
/* ------------------------------------------------------------------ */
static void start_upload(const char *filename)
{
    strncpy(g_ul_filename, filename, sizeof(g_ul_filename) - 1);

    char ul_path[520];
    normalize_local_path(filename, ul_path, sizeof(ul_path));
    g_ul_fd = open(ul_path, O_RDONLY);
    if (g_ul_fd < 0) {
        str_data_t *err = make_str_data("Local file not found");
        if (err) lv_async_call(cb_show_error_popup, err);
        g_state = ST_IDLE;
        return;
    }
    /* get file size */
    off_t sz = lseek(g_ul_fd, 0, SEEK_END);
    lseek(g_ul_fd, 0, SEEK_SET);
    g_ul_total = (int)sz;
    g_ul_sent  = 0;

    g_transfer_progress.active  = true;
    g_transfer_progress.is_upload = true;
    g_transfer_progress.percent = 0;
    g_transfer_progress.current_bytes = 0;
    g_transfer_progress.total_bytes = g_ul_total;
    strncpy(g_transfer_progress.filename, filename,
            sizeof(g_transfer_progress.filename) - 1);
    g_last_progress = -1;
    g_active_transfers++;

    g_state = ST_UPLOADING;

    /* NOTE: progress popup is created synchronously by UI button handlers
     * via ui_show_progress_batch(). Network layer only drives data transfer. */
}

static void update_ul_progress(void)
{
    int pct = (g_ul_total > 0) ? (g_ul_sent * 100 / g_ul_total) : 0;
    /* update every 2% increment (or at 100% final), avoid flooding async calls */
    if (pct - g_last_progress < 2 && pct < 100) return;
    g_last_progress = pct;

    transfer_progress_t *tp = (transfer_progress_t *)malloc(sizeof(transfer_progress_t));
    if (!tp) return;
    tp->percent       = pct;
    tp->current_bytes = g_ul_sent;
    tp->total_bytes   = g_ul_total;
    tp->active        = true;
    tp->is_upload     = true;
    strncpy(tp->filename, g_ul_filename, sizeof(tp->filename) - 1);
    lv_async_call(cb_dl_progress, tp);
}

static void finish_upload(bool success)
{
    if (g_ul_fd >= 0) { close(g_ul_fd); g_ul_fd = -1; }
    g_ul_sent  = 0;
    g_ul_total = 0;
    g_last_progress = -1;
    g_transfer_progress.active = false;
    g_state = ST_IDLE;
    g_transfer_cancelled = false;
    if (g_active_transfers > 0) g_active_transfers--;

    printf("[DEBUG] finish_upload: %s success=%d batch=%d done=%d/%d queue=%d\n",
           g_ul_filename, success, g_batch_active, g_batch_done, g_batch_total, g_tx_queue.count);

    /* per-file report to batch panel */
    {
        tx_done_data_t *d = (tx_done_data_t *)malloc(sizeof(*d));
        if (d) {
            strncpy(d->filename, g_ul_filename, sizeof(d->filename) - 1);
            d->filename[sizeof(d->filename) - 1] = '\0';
            d->success   = success;
            d->is_upload = true;
            lv_async_call(cb_tx_done, d);
        }
    }

    if (g_batch_active) {
        g_batch_done++;
        /* don't show "Uploaded" yet -- batch_check_complete handles it */
    } else {
        str_data_t *msg = make_str_data(success
            ? "Uploaded"
            : "Upload failed");
        if (msg) lv_async_call(cb_ul_done, msg);
    }
}

/* ------------------------------------------------------------------ */
/*  Handle a download chunk (raw bytes from socket)                  */
/* ------------------------------------------------------------------ */
static int handle_download_chunk(void)
{
    unsigned char buf[CHUNK_SIZE];
    int remaining = g_dl_total - g_dl_received;
    if (remaining <= 0) return 1;                     /* transfer complete */

    int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    int r = (int)read(g_sockfd, buf, (size_t)to_read);
    if (r <= 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;                                 /* retry later */
        return -1;                                    /* connection error */
    }
    int w = (int)write(g_dl_fd, buf, (size_t)r);
    if (w < 0) return -1;
    g_dl_received += w;
    update_dl_progress();
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Handle an upload chunk (read file, write to socket)              */
/* ------------------------------------------------------------------ */
static int handle_upload_chunk(void)
{
    unsigned char buf[CHUNK_SIZE];
    int remaining = g_ul_total - g_ul_sent;
    if (remaining <= 0) return 1;                     /* transfer complete */

    int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    int r = (int)read(g_ul_fd, buf, (size_t)to_read);
    if (r <= 0) return -1;                            /* local file error */
    int w = (int)write(g_sockfd, buf, (size_t)r);
    if (w < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;                                 /* retry later */
        return -1;                                    /* network error */
    }
    g_ul_sent += w;
    update_ul_progress();
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Network thread main                                               */
/* ------------------------------------------------------------------ */
void *network_thread_func(void *arg)
{
    char    ip[64]       = {0};
    char    port[16]     = {0};
    char    username[64] = {0};
    char    password[64] = {0};

    /* ---- parse connection parameters ---- */
    if (arg) {
        char **params = (char **)arg;
        if (params[0]) strncpy(ip,       params[0], sizeof(ip) - 1);
        if (params[1]) strncpy(port,     params[1], sizeof(port) - 1);
        if (params[2]) strncpy(username, params[2], sizeof(username) - 1);
        if (params[3]) strncpy(password, params[3], sizeof(password) - 1);
        /* free now -- we've copied everything to local buffers */
        free(params[0]); free(params[1]); free(params[2]); free(params[3]);
        free(params);
    }

    /* save credentials globally for transfer workers */
    strncpy(g_login_ip,   ip,       sizeof(g_login_ip) - 1);
    strncpy(g_login_port, port,     sizeof(g_login_port) - 1);
    strncpy(g_login_user, username, sizeof(g_login_user) - 1);
    strncpy(g_login_pass, password, sizeof(g_login_pass) - 1);

    /* ---- create socket and connect ---- */
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) {
        lv_async_call(cb_show_error_popup, make_str_data("socket() failed"));
        g_network_running = false;
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(g_sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lv_async_call(cb_show_error_popup, make_str_data("Connect failed"));
        close(g_sockfd);
        g_sockfd = -1;
        g_network_running = false;
        return NULL;
    }

    /* ---- send LOGIN command ---- */
    /* Format:  0xC0 + pkg_len + cmd_no + arg1_len + arg1 + arg2_len + arg2 + 0xC0 */
    int ulen = (int)strlen(username);
    int plen = (int)strlen(password);
    int arg_total = 4 + ulen + 4 + plen;
    unsigned char *login_arg = (unsigned char *)malloc((size_t)arg_total);
    if (!login_arg) { close(g_sockfd); g_sockfd = -1; g_network_running = false; return NULL; }
    put_le32(login_arg,      ulen);
    memcpy(login_arg + 4, username, (size_t)ulen);
    put_le32(login_arg + 4 + ulen, plen);
    memcpy(login_arg + 4 + ulen + 4, password, (size_t)plen);

    int pkt_len;
    unsigned char *pkt = build_cmd(FTP_CMD_LOGIN, login_arg, arg_total, &pkt_len);
    free(login_arg);
    if (!pkt) { close(g_sockfd); g_sockfd = -1; g_network_running = false; return NULL; }
    write(g_sockfd, pkt, (size_t)pkt_len);
    free(pkt);

    /* ---- read LOGIN response ---- */
    int rlen;
    unsigned char *rsp = read_packet(g_sockfd, &rlen);
    if (!rsp) {
        lv_async_call(cb_show_error_popup, make_str_data("No login response"));
        close(g_sockfd); g_sockfd = -1;
        g_network_running = false;
        return NULL;
    }
    resp_t resp;
    parse_response(rsp, rlen, &resp);
    g_login_ok = (resp.cmd_no == FTP_CMD_LOGIN && resp.res_result == 1);
    if (g_login_ok) {
        /* Session info is in the response data (if any) */
        int info_len = resp.res_len - 1;  /* minus res_result byte */
        if (info_len > 0 && info_len < (int)sizeof(g_session_info)) {
            memcpy(g_session_info, resp.res_data, (size_t)info_len);
            g_session_info[info_len] = '\0';
        } else {
            snprintf(g_session_info, sizeof(g_session_info), "SID-%d",
                     (int)(g_sockfd & 0xFFFF));
        }
        lv_async_call(cb_login_result, make_str_data("Login OK"));
    } else {
        lv_async_call(cb_login_result, make_str_data("Login rejected"));
        close(g_sockfd); g_sockfd = -1;
        g_network_running = false;
        free(rsp);
        return NULL;
    }
    free(rsp);

    /* initialize transfer queue */
    tx_queue_init(&g_tx_queue);

    /* ---- main RX loop ---- */
    g_state = ST_IDLE;
    while (g_network_running) {
        /* ============================================================ */
        /*  Priority 1: ST_DOWNLOADING - receive raw file stream        */
        /* ============================================================ */
        if (g_state == ST_DOWNLOADING) {
            if (g_transfer_cancelled) {
                finish_download(false);
                g_transfer_cancelled = false;
                continue;
            }
            if (g_dl_received >= g_dl_total) {
                finish_download(true);
                continue;
            }
            int rc = handle_download_chunk();
            if (rc < 0)  finish_download(false);
            continue;       /* tight loop: keep reading until done/error */
        }

        /* ============================================================ */
        /*  Priority 2: ST_UPLOADING - send raw file stream             */
        /* ============================================================ */
        if (g_state == ST_UPLOADING) {
            if (g_transfer_cancelled) {
                finish_upload(false);
                g_transfer_cancelled = false;
                continue;
            }
            if (g_ul_sent >= g_ul_total) {
                finish_upload(true);
                continue;
            }
            int rc = handle_upload_chunk();
            if (rc < 0)  finish_upload(false);
            continue;       /* tight loop: keep writing until done/error */
        }

        /* ============================================================ */
        /*  Priority 3: ST_IDLE - start queued transfers               */
        /* ============================================================ */
        if (g_state == ST_IDLE) {
            if (g_transfer_cancelled) { g_transfer_cancelled = false; }
            if (g_tx_queue.count > 0 && !g_tx_queue.cancelled) {
                printf("[DEBUG] main loop: starting next queued transfer (queue=%d)\n",
                       g_tx_queue.count);
                batch_start_next();
                continue;
            }
            batch_check_complete();
        }

        /* ============================================================ */
        /*  Priority 4: ST_WAIT / ST_IDLE - poll for server response   */
        /* ============================================================ */
        {
            struct pollfd pfd = { .fd = g_sockfd, .events = POLLIN };
            int polltime = (g_state == ST_IDLE && g_tx_queue.count > 0) ? 0 : 500;
            int pr = poll(&pfd, 1, polltime);
            if (pr < 0) break;
            if (pr == 0) continue;

            unsigned char *payload = read_packet(g_sockfd, &rlen);
            if (!payload) break;

            resp_t rsp2;
            if (!parse_response(payload, rlen, &rsp2)) {
                free(payload);
                continue;
            }

            /* Dispatch based on current state expectation */
            if (g_state == ST_WAIT_GET_RESP) {
                if (rsp2.cmd_no == FTP_CMD_GET && rsp2.res_result == 1) {
                    int filesize = get_le32(rsp2.res_data);
                    start_download(g_dl_filename, filesize);
                } else {
                    str_data_t *err = make_str_data("Server: file not found");
                    if (err) lv_async_call(cb_show_error_popup, err);
                    g_state = ST_IDLE;
                    if (g_batch_active) { g_batch_done++; }
                }
                free(payload);
                continue;
            }

            if (g_state == ST_WAIT_PUT_RESP) {
                if (rsp2.cmd_no == FTP_CMD_PUT && rsp2.res_result == 1) {
                    if (g_ul_filename[0])
                        start_upload(g_ul_filename);
                    else {
                        str_data_t *err = make_str_data("Upload target missing");
                        if (err) lv_async_call(cb_show_error_popup, err);
                        g_state = ST_IDLE;
                    }
                } else {
                    str_data_t *err = make_str_data("Server rejected upload");
                    if (err) lv_async_call(cb_show_error_popup, err);
                    g_state = ST_IDLE;
                    if (g_batch_active) { g_batch_done++; }
                }
                free(payload);
                continue;
            }

            /* ---- ST_IDLE: async notifications (LS, unsolicited) ---- */
            switch (rsp2.cmd_no) {
            case FTP_CMD_LS:
                if (rsp2.res_result == 1) {
                    int dlen = rsp2.res_len - 1;
                    char *filelist = (char *)malloc((size_t)(dlen + 1));
                    if (filelist) {
                        memcpy(filelist, rsp2.res_data, (size_t)dlen);
                        filelist[dlen] = '\0';
                        lv_async_call(ui_update_file_list_cb, filelist);
                    }
                }
                break;
            case FTP_CMD_BYE:
                break;
            default:
                break;
            }
            free(payload);
        }
    }

    /* ---- cleanup ---- */
    tx_queue_cancel_all(&g_tx_queue);
    tx_queue_destroy(&g_tx_queue);
    if (g_dl_fd >= 0) { close(g_dl_fd); g_dl_fd = -1; }
    if (g_ul_fd >= 0) { close(g_ul_fd); g_ul_fd = -1; }
    if (g_sockfd >= 0) { close(g_sockfd); g_sockfd = -1; }

    g_login_ok         = false;
    g_network_running  = false;
    g_transfer_progress.active = false;
    g_state            = ST_IDLE;

    lv_async_call(cb_disconnected, NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API implementations (called from UI thread)                */
/* ------------------------------------------------------------------ */

bool network_start_connect(const char *ip, const char *port,
                           const char *username, const char *password)
{
    if (g_network_running) return false;

    /* Allocate copies of the strings so the network thread owns them.
     * The UI thread's textarea buffers may be freed on screen switch. */
    char **params = (char **)malloc(4 * sizeof(char *));
    if (!params) return false;
    params[0] = strdup(ip       ? ip       : "");
    params[1] = strdup(port     ? port     : "");
    params[2] = strdup(username ? username : "");
    params[3] = strdup(password ? password : "");

    g_login_ok        = false;
    g_network_running = true;
    g_session_info[0] = '\0';

    if (pthread_create(&g_net_thread, NULL, network_thread_func,
                       (void *)params) != 0) {
        g_network_running = false;
        free(params[0]); free(params[1]); free(params[2]); free(params[3]);
        free(params);
        return false;
    }
    pthread_detach(g_net_thread);
    return true;
}

void network_disconnect(void)
{
    tx_queue_cancel_all(&g_tx_queue);
    g_batch_active = false;

    if (!g_network_running || g_sockfd < 0) return;

    /* Send BYE */
    int len;
    unsigned char *pkt = build_cmd(FTP_CMD_BYE, NULL, 0, &len);
    if (pkt) {
        write(g_sockfd, pkt, (size_t)len);
        free(pkt);
    }
    usleep(100000);  /* 100ms for BYE to be sent */
    g_network_running = false;

    /* break the blocking read in network thread */
    shutdown(g_sockfd, SHUT_RDWR);
}

bool network_cmd_ls(void)
{
    if (!g_network_running || g_sockfd < 0) return false;
    int len;
    unsigned char *pkt = build_cmd(FTP_CMD_LS, NULL, 0, &len);
    if (!pkt) return false;
    write(g_sockfd, pkt, (size_t)len);
    free(pkt);
    return true;
}

/* ---- Single-file: send directly on main socket (core transfer functions) ---- */

bool network_cmd_get(const char *filename)
{
    if (!g_network_running || g_sockfd < 0 || !filename) return false;

    strncpy(g_dl_filename, filename, sizeof(g_dl_filename) - 1);
    g_state = ST_WAIT_GET_RESP;
    g_dl_filename[0] = '\0';
    strncpy(g_dl_filename, filename, sizeof(g_dl_filename) - 1);

    int len;
    unsigned char *pkt = build_cmd_with_str(FTP_CMD_GET, filename, &len);
    if (!pkt) return false;
    write(g_sockfd, pkt, (size_t)len);
    free(pkt);
    return true;
}

bool network_cmd_put(const char *filename)
{
    if (!g_network_running || g_sockfd < 0 || !filename) return false;

    /* check local file exists under ./client/ */
    struct stat st;
    char path[520];
    normalize_local_path(filename, path, sizeof(path));
    if (stat(path, &st) != 0) {
        return false;
    }
    int filesize = (int)st.st_size;

    g_ul_filename[0] = '\0';
    strncpy(g_ul_filename, filename, sizeof(g_ul_filename) - 1);
    g_state = ST_WAIT_PUT_RESP;

    int len;
    unsigned char *pkt = build_cmd_put(filename, filesize, &len);
    if (!pkt) return false;
    write(g_sockfd, pkt, (size_t)len);
    free(pkt);
    return true;
}

/* ---- Multi-file: push all to queue, start the first one ---- */

bool network_cmd_get_multi(const char **filenames, int count)
{
    if (!g_network_running || !filenames || count <= 0) return false;
    if (count > MAX_SELECTED_FILES) count = MAX_SELECTED_FILES;

    /* reset cancelled flag so queue is usable after a prior cancel */
    tx_queue_reset(&g_tx_queue);

    /* push all to queue, count actual enqueued */
    int actual = 0;
    for (int i = 0; i < count; i++) {
        if (!filenames[i] || strlen(filenames[i]) == 0) continue;
        transfer_task_t task;
        memset(&task, 0, sizeof(task));
        strncpy(task.filename, filenames[i], sizeof(task.filename) - 1);
        task.is_upload = false;
        if (tx_queue_push(&g_tx_queue, &task))
            actual++;
    }

    if (actual > 0) {
        g_batch_total  = actual;
        g_batch_done   = 0;
        g_batch_active = true;
        printf("[DEBUG] get_multi: enqueued %d files\n", actual);
    }
    return actual > 0;
}

bool network_cmd_put_multi(const char **filenames, int count)
{
    if (!g_network_running || !filenames || count <= 0) return false;
    if (count > MAX_SELECTED_FILES) count = MAX_SELECTED_FILES;

    /* reset cancelled flag so queue is usable after a prior cancel */
    tx_queue_reset(&g_tx_queue);

    struct stat st;
    int valid_count = 0;

    /* validate and push */
    for (int i = 0; i < count; i++) {
        if (!filenames[i] || strlen(filenames[i]) == 0) continue;
        char fpath[520];
        normalize_local_path(filenames[i], fpath, sizeof(fpath));
        if (stat(fpath, &st) != 0) {
            str_data_t *err = make_str_data("file unexist");
            if (err) lv_async_call(cb_show_error_popup, err);
            continue;
        }
        transfer_task_t task;
        memset(&task, 0, sizeof(task));
        strncpy(task.filename, filenames[i], sizeof(task.filename) - 1);
        task.is_upload = true;
        tx_queue_push(&g_tx_queue, &task);
        valid_count++;
    }

    if (valid_count > 0) {
        g_batch_total  = valid_count;
        g_batch_done   = 0;
        g_batch_active = true;
        printf("[DEBUG] put_multi: enqueued %d files\n", valid_count);
    }
    return valid_count > 0;
}

void network_cancel_transfer(void)
{
    g_transfer_cancelled = true;
    tx_queue_cancel_all(&g_tx_queue);
    g_batch_active = false;
    g_active_transfers = 0;
}
