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
    int offset = 0;
    while (offset < payload_len) {
        if (read(fd, &ch, 1) <= 0) { free(payload); return NULL; }
        if (ch == 0xC0) break;       /* trailer reached */
        payload[offset++] = ch;
    }
    if (offset != payload_len) {
        /* data contained a 0xC0 that wasn't really the trailer */
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
    show_progress_data_t *d = (show_progress_data_t *)data;
    ui_show_progress(d->filename, d->is_upload);
    free(d);
}

static void cb_ul_done(void *data)
{
    str_data_t *d = (str_data_t *)data;
    ui_hide_progress();
    ui_set_status(d ? d->text : "Uploaded");
    ui_restore_status_after_delay();
    free(d);
}

/* ---- Transfer-task data types and async callbacks ---- */

typedef struct {
    char filename[256];
    int  percent;
    int  current_bytes;
    int  total_bytes;
    bool is_upload;
} tx_progress_data_t;

typedef struct {
    char filename[256];
    bool success;
    bool is_upload;
} tx_done_data_t;

static void cb_tx_progress(void *data)
{
    tx_progress_data_t *d = (tx_progress_data_t *)data;
    ui_update_transfer_progress(d->filename, d->percent,
                                 d->current_bytes, d->total_bytes,
                                 d->is_upload);
    free(d);
}

static void cb_tx_done(void *data)
{
    tx_done_data_t *d = (tx_done_data_t *)data;
    ui_on_transfer_done(d->filename, d->success, d->is_upload);
    free(d);
}

static void cb_all_done(void *data)
{
    (void)data;
    __sync_synchronize();
    int active = g_active_transfers;
    if (active <= 0 && g_tx_queue.count == 0) {
        if (g_tx_queue.cancelled)
            ui_set_status("Transfer cancelled");
        else
            ui_set_status("Transfer complete");
        ui_hide_progress();
        ui_restore_status_after_delay();
    }
}

static void cb_show_progress_batch(void *data)
{
    (void)data;
    ui_show_progress_batch();
}

static void cb_show_error_popup(void *data)
{
    str_data_t *d = (str_data_t *)data;
    if (d) {
        ui_show_error_popup(d->text);
        free(d);
    }
}

static void report_tx_progress(const char *filename, int pct,
                                int cur, int total, bool is_upload)
{
    tx_progress_data_t *d = (tx_progress_data_t *)malloc(sizeof(*d));
    if (!d) return;
    strncpy(d->filename, filename, sizeof(d->filename) - 1);
    d->filename[sizeof(d->filename) - 1] = '\0';
    d->percent       = pct;
    d->current_bytes = cur;
    d->total_bytes   = total;
    d->is_upload     = is_upload;
    lv_async_call(cb_tx_progress, d);
}

static void report_tx_complete(const char *filename, bool success,
                                bool is_upload)
{
    tx_done_data_t *d = (tx_done_data_t *)malloc(sizeof(*d));
    if (!d) return;
    strncpy(d->filename, filename, sizeof(d->filename) - 1);
    d->filename[sizeof(d->filename) - 1] = '\0';
    d->success   = success;
    d->is_upload = is_upload;
    lv_async_call(cb_tx_done, d);
}

static void report_all_done(void)
{
    lv_async_call(cb_all_done, NULL);
}

/* ================================================================== */
/*  Transfer worker helpers                                           */
/* ================================================================== */

static bool worker_login(int sock)
{
    int ulen = (int)strlen(g_login_user);
    int plen = (int)strlen(g_login_pass);
    int arg_total = 4 + ulen + 4 + plen;

    unsigned char *login_arg = (unsigned char *)malloc((size_t)arg_total);
    if (!login_arg) return false;

    put_le32(login_arg, ulen);
    memcpy(login_arg + 4, g_login_user, (size_t)ulen);
    put_le32(login_arg + 4 + ulen, plen);
    memcpy(login_arg + 4 + ulen + 4, g_login_pass, (size_t)plen);

    int pkt_len;
    unsigned char *pkt = build_cmd(FTP_CMD_LOGIN, login_arg, arg_total, &pkt_len);
    free(login_arg);
    if (!pkt) return false;

    write(sock, pkt, (size_t)pkt_len);
    free(pkt);

    int rlen;
    unsigned char *rsp = read_packet(sock, &rlen);
    if (!rsp) return false;

    resp_t resp;
    bool ok = parse_response(rsp, rlen, &resp)
              && resp.cmd_no == FTP_CMD_LOGIN
              && resp.res_result == 1;
    free(rsp);
    return ok;
}

static void do_download_task(const transfer_task_t *task)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        report_tx_complete(task->filename, false, false);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(g_login_port));
    inet_pton(AF_INET, g_login_ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        report_tx_complete(task->filename, false, false);
        return;
    }

    if (!worker_login(sock)) {
        close(sock);
        report_tx_complete(task->filename, false, false);
        return;
    }

    int len;
    unsigned char *pkt = build_cmd_with_str(FTP_CMD_GET, task->filename, &len);
    if (!pkt) { close(sock); report_tx_complete(task->filename, false, false); return; }
    write(sock, pkt, (size_t)len);
    free(pkt);

    int rlen;
    unsigned char *rsp = read_packet(sock, &rlen);
    if (!rsp) { close(sock); report_tx_complete(task->filename, false, false); return; }

    resp_t resp;
    bool parsed = parse_response(rsp, rlen, &resp);
    if (!parsed || resp.cmd_no != FTP_CMD_GET || resp.res_result != 1) {
        free(rsp);
        close(sock);
        report_tx_complete(task->filename, false, false);
        return;
    }

    int filesize = get_le32(resp.res_data);
    free(rsp);

    mkdir("./load", 0755);
    char path[520];
    snprintf(path, sizeof(path), "./load/%s", task->filename);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        close(sock);
        report_tx_complete(task->filename, false, false);
        return;
    }

    unsigned char buf[CHUNK_SIZE];
    int received  = 0;
    int last_pct  = -1;

    while (received < filesize) {
        if (g_tx_queue.cancelled) break;

        int remaining = filesize - received;
        int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        int r = (int)read(sock, buf, (size_t)to_read);
        if (r <= 0) break;
        ssize_t w = write(fd, buf, (size_t)r);
        if (w < 0) break;
        received += (int)w;

        int pct = (filesize > 0) ? (received * 100 / filesize) : 0;
        if (pct - last_pct >= 2 || pct >= 100) {
            last_pct = pct;
            report_tx_progress(task->filename, pct, received, filesize, false);
        }
    }

    close(fd);
    close(sock);

    if (g_tx_queue.cancelled) {
        unlink(path);
        report_tx_complete(task->filename, false, false);
    } else if (received >= filesize) {
        report_tx_complete(task->filename, true, false);
    } else {
        report_tx_complete(task->filename, false, false);
    }
}

static void do_upload_task(const transfer_task_t *task)
{
    struct stat st;
    if (stat(task->filename, &st) != 0) {
        report_tx_complete(task->filename, false, true);
        return;
    }
    int filesize = (int)st.st_size;

    int local_fd = open(task->filename, O_RDONLY);
    if (local_fd < 0) {
        report_tx_complete(task->filename, false, true);
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        close(local_fd);
        report_tx_complete(task->filename, false, true);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(g_login_port));
    inet_pton(AF_INET, g_login_ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        close(local_fd);
        report_tx_complete(task->filename, false, true);
        return;
    }

    if (!worker_login(sock)) {
        close(sock);
        close(local_fd);
        report_tx_complete(task->filename, false, true);
        return;
    }

    int slen = (int)strlen(task->filename);
    int arg_len = 4 + slen + 4;
    unsigned char *arg = (unsigned char *)malloc((size_t)arg_len);
    if (!arg) { close(sock); close(local_fd); report_tx_complete(task->filename, false, true); return; }
    put_le32(arg, slen);
    memcpy(arg + 4, task->filename, (size_t)slen);
    put_le32(arg + 4 + slen, filesize);

    int pkt_len;
    unsigned char *pkt = build_cmd(FTP_CMD_PUT, arg, arg_len, &pkt_len);
    free(arg);
    if (!pkt) { close(sock); close(local_fd); report_tx_complete(task->filename, false, true); return; }
    write(sock, pkt, (size_t)pkt_len);
    free(pkt);

    int rlen;
    unsigned char *rsp = read_packet(sock, &rlen);
    if (!rsp) { close(sock); close(local_fd); report_tx_complete(task->filename, false, true); return; }

    resp_t resp;
    bool parsed = parse_response(rsp, rlen, &resp);
    free(rsp);
    if (!parsed || resp.cmd_no != FTP_CMD_PUT || resp.res_result != 1) {
        close(sock);
        close(local_fd);
        report_tx_complete(task->filename, false, true);
        return;
    }

    unsigned char buf[CHUNK_SIZE];
    int sent     = 0;
    int last_pct = -1;

    while (sent < filesize) {
        if (g_tx_queue.cancelled) break;

        int remaining = filesize - sent;
        int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        int r = (int)read(local_fd, buf, (size_t)to_read);
        if (r <= 0) break;
        ssize_t w = write(sock, buf, (size_t)r);
        if (w < 0) break;
        sent += (int)w;

        int pct = (filesize > 0) ? (sent * 100 / filesize) : 0;
        if (pct - last_pct >= 2 || pct >= 100) {
            last_pct = pct;
            report_tx_progress(task->filename, pct, sent, filesize, true);
        }
    }

    close(local_fd);
    close(sock);

    if (g_tx_queue.cancelled) {
        report_tx_complete(task->filename, false, true);
    } else if (sent >= filesize) {
        report_tx_complete(task->filename, true, true);
    } else {
        report_tx_complete(task->filename, false, true);
    }
}

static void *transfer_worker_func(void *arg)
{
    transfer_worker_t *worker = (transfer_worker_t *)arg;
    transfer_task_t task;

    while (worker->running) {
        if (!tx_queue_pop(&g_tx_queue, &task))
            break;

        __sync_fetch_and_add(&g_active_transfers, 1);

        if (task.is_upload)
            do_upload_task(&task);
        else
            do_download_task(&task);

        __sync_fetch_and_sub(&g_active_transfers, 1);
    }

    if (g_active_transfers <= 0 && g_tx_queue.count == 0)
        report_all_done();

    return NULL;
}

void transfer_pool_init(void)
{
    tx_queue_init(&g_tx_queue);

    for (int i = 0; i < TRANSFER_POOL_SIZE; i++) {
        g_tx_workers[i].id      = i;
        g_tx_workers[i].running = true;
        if (pthread_create(&g_tx_workers[i].thread, NULL,
                           transfer_worker_func, &g_tx_workers[i]) != 0) {
            g_tx_workers[i].running = false;
        } else {
            pthread_detach(g_tx_workers[i].thread);
        }
    }
}

void transfer_pool_stop(void)
{
    tx_queue_cancel_all(&g_tx_queue);

    for (int i = 0; i < TRANSFER_POOL_SIZE; i++) {
        g_tx_workers[i].running = false;
    }
    pthread_cond_broadcast(&g_tx_queue.cond);
    usleep(100000);

    tx_queue_destroy(&g_tx_queue);
    g_active_transfers = 0;
}

/* ------------------------------------------------------------------ */
/*  Download state management (called from network thread)            */
/* ------------------------------------------------------------------ */
static void start_download(const char *filename, int filesize)
{
    strncpy(g_dl_filename, filename, sizeof(g_dl_filename) - 1);
    g_dl_total    = filesize;
    g_dl_received = 0;
    g_last_progress = -1;

    /* create / truncate local file */
    mkdir("./load", 0755);
    char dl_path[520];
    snprintf(dl_path, sizeof(dl_path), "./load/%s", filename);
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

    /* show progress dialog — must go through async call (UI thread safety) */
    show_progress_data_t *spd = (show_progress_data_t *)malloc(sizeof(show_progress_data_t));
    if (spd) {
        strncpy(spd->filename, filename, sizeof(spd->filename) - 1);
        spd->filename[sizeof(spd->filename) - 1] = '\0';
        spd->is_upload = false;
        lv_async_call(cb_show_progress, spd);
    }
}

static void update_dl_progress(void)
{
    int pct = (g_dl_total > 0) ? (g_dl_received * 100 / g_dl_total) : 0;
    /* update every 2% increment (or at 100% final), avoid flooding async calls */
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

    str_data_t *msg = make_str_data(success
        ? "Downloaded"
        : "Download failed");
    if (msg) lv_async_call(cb_dl_done, msg);
}

/* ------------------------------------------------------------------ */
/*  Upload state management (called from network thread)              */
/* ------------------------------------------------------------------ */
static void start_upload(const char *filename)
{
    strncpy(g_ul_filename, filename, sizeof(g_ul_filename) - 1);

    mkdir("./copy", 0755);
    char ul_path[520];
    snprintf(ul_path, sizeof(ul_path), "./copy/%s", filename);
    g_ul_fd = open(ul_path, O_RDONLY);
    if (g_ul_fd < 0) {
        str_data_t *err = make_str_data("Local file not found");
        if (err) lv_async_call(cb_error, err);
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

    g_state = ST_UPLOADING;

    /* show progress dialog — must go through async call (UI thread safety) */
    show_progress_data_t *spd = (show_progress_data_t *)malloc(sizeof(show_progress_data_t));
    if (spd) {
        strncpy(spd->filename, filename, sizeof(spd->filename) - 1);
        spd->filename[sizeof(spd->filename) - 1] = '\0';
        spd->is_upload = true;
        lv_async_call(cb_show_progress, spd);
    }
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

    str_data_t *msg = make_str_data(success
        ? "Uploaded"
        : "Upload failed");
    if (msg) lv_async_call(cb_ul_done, msg);
}

/* ------------------------------------------------------------------ */
/*  Handle a download chunk (raw bytes from socket)                  */
/* ------------------------------------------------------------------ */
static int handle_download_chunk(void)
{
    unsigned char buf[CHUNK_SIZE];
    int remaining = g_dl_total - g_dl_received;
    int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    int r = read_n(g_sockfd, buf, to_read);
    if (r <= 0) return -1;
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
    int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    int r = (int)read(g_ul_fd, buf, (size_t)to_read);
    if (r <= 0) return -1;
    int w = (int)write(g_sockfd, buf, (size_t)r);
    if (w < 0) return -1;
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
        /* free now — we've copied everything to local buffers */
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
        lv_async_call(cb_error, make_str_data("socket() failed"));
        g_network_running = false;
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(g_sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lv_async_call(cb_error, make_str_data("Connect failed"));
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
        lv_async_call(cb_error, make_str_data("No login response"));
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

    /* initialize transfer pool after successful login */
    transfer_pool_init();

    /* ---- main RX loop ---- */
    g_state = ST_IDLE;
    while (g_network_running) {
        if (g_state == ST_DOWNLOADING) {
            if (g_dl_received >= g_dl_total) {
                finish_download(true);
                continue;
            }
            if (handle_download_chunk() < 0) {
                finish_download(false);
            }
            continue;
        }

        if (g_state == ST_UPLOADING) {
            if (g_ul_sent >= g_ul_total) {
                finish_upload(true);
                continue;
            }
            if (handle_upload_chunk() < 0) {
                finish_upload(false);
            }
            continue;
        }

        /* ---- IDLE / waiting for a response packet ---- */
        /* poll with timeout so we can check g_network_running */
        struct pollfd pfd = { .fd = g_sockfd, .events = POLLIN };
        int pr = poll(&pfd, 1, 500);
        if (pr < 0) break;
        if (pr == 0) continue;           /* timeout, re-check flag */

        unsigned char *payload = read_packet(g_sockfd, &rlen);
        if (!payload) break;             /* connection lost */

        resp_t rsp2;
        if (!parse_response(payload, rlen, &rsp2)) {
            free(payload);
            continue;
        }

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

        case FTP_CMD_GET:
            if (rsp2.res_result == 1) {
                int filesize = get_le32(rsp2.res_data);
                start_download(g_dl_filename, filesize);
            } else {
                str_data_t *err = make_str_data("Server: file not found");
                if (err) lv_async_call(cb_error, err);
            }
            break;

        case FTP_CMD_PUT:
            if (rsp2.res_result == 1) {
                start_upload(g_ul_filename);
            } else {
                str_data_t *err = make_str_data("Server rejected upload");
                if (err) lv_async_call(cb_error, err);
            }
            break;

        case FTP_CMD_BYE:
            /* server acknowledges �?we'll disconnect on our side */
            break;

        default:
            break;
        }
        free(payload);
    }

    /* ---- cleanup ---- */
    transfer_pool_stop();
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
    transfer_pool_stop();

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

bool network_cmd_get(const char *filename)
{
    return network_cmd_get_multi(&filename, 1);
}

bool network_cmd_get_multi(const char **filenames, int count)
{
    if (!g_network_running || !filenames || count <= 0) return false;
    if (count > MAX_SELECTED_FILES) count = MAX_SELECTED_FILES;

    lv_async_call(cb_show_progress_batch, NULL);

    int enqueued = 0;
    for (int i = 0; i < count; i++) {
        if (!filenames[i] || strlen(filenames[i]) == 0) continue;

        transfer_task_t task;
        memset(&task, 0, sizeof(task));
        strncpy(task.filename, filenames[i], sizeof(task.filename) - 1);
        task.is_upload = false;

        if (tx_queue_push(&g_tx_queue, &task))
            enqueued++;
    }
    return enqueued > 0;
}

bool network_cmd_put(const char *filename)
{
    return network_cmd_put_multi(&filename, 1);
}

bool network_cmd_put_multi(const char **filenames, int count)
{
    if (!g_network_running || !filenames || count <= 0) return false;
    if (count > MAX_SELECTED_FILES) count = MAX_SELECTED_FILES;

    struct stat st;
    int valid_count = 0;
    const char *valid_files[MAX_SELECTED_FILES];

    for (int i = 0; i < count; i++) {
        if (!filenames[i] || strlen(filenames[i]) == 0) continue;
        if (stat(filenames[i], &st) != 0) {
            str_data_t *err = make_str_data("file unexist");
            if (err) lv_async_call(cb_show_error_popup, err);
            continue;
        }
        valid_files[valid_count++] = filenames[i];
    }

    if (valid_count == 0) return false;

    lv_async_call(cb_show_progress_batch, NULL);

    int enqueued = 0;
    for (int i = 0; i < valid_count; i++) {
        transfer_task_t task;
        memset(&task, 0, sizeof(task));
        strncpy(task.filename, valid_files[i], sizeof(task.filename) - 1);
        task.is_upload = true;

        if (tx_queue_push(&g_tx_queue, &task))
            enqueued++;
    }
    return enqueued > 0;
}

void network_cancel_transfer(void)
{
    tx_queue_cancel_all(&g_tx_queue);
    g_active_transfers = 0;
}
