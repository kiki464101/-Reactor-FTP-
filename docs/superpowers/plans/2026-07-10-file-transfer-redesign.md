# File Transfer Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Multi-selection file transfer with multi-threaded transfer pool, progress popup, and status bar lifecycle.

**Architecture:** UI thread manages multi-select lists (remote/local) with green toggle. Transfer thread pool (3 workers) with independent socket connections per task pulls from a thread-safe FIFO queue. Main network thread handles only LS/BYE commands. Each transfer worker opens its own TCP socket, logs in, and transfers a single file, reporting progress to UI via `lv_async_call`.

**Tech Stack:** C, pthreads, LVGL, TCP sockets, Linux

## Global Constraints

- Save downloaded files to `client/load/`
- Upload source directory: `client/` (excludes subdirectories)
- Upload target directory: `server/copy/`
- Transfer thread pool size: 3 workers
- Max selected files: 128
- Max queue size: 256
- Status bar shows "Downloading..."/"Uploading..." during transfer, "Downloaded"/"Uploaded" on completion, auto-restore after 3s
- "file unexist" popup with centered Close button when upload source missing
- Progress popup closable without aborting transfer; Cancel button aborts all

---

## File Structure

| File | Role |
|------|------|
| `client/network_task.h` | Transfer queue + thread pool types, global declarations, updated public API |
| `client/network_task.c` | Queue implementation, thread pool, per-task independent-socket transfers, progress callbacks |
| `client/ui_manager.h` | Multi-select API, updated progress/status declarations |
| `client/ui_manager.c` | Multi-select toggle UI, local file scan from `client/`, new button layout, multi-transfer progress popup, status bar lifecycle, error popup |

---

### Task 1: Add transfer queue, thread pool, and credential types to network_task.h

**Files:**
- Modify: `client/network_task.h`

**Interfaces:**
- Produces: `transfer_task_t`, `transfer_queue_t`, `transfer_worker_t`, `g_tx_queue`, `g_tx_workers`, `g_login_ip/port/user/pass`, `g_active_transfers`, queue API, thread pool lifecycle API, `network_cmd_get_multi`, `network_cmd_put_multi`, `network_cancel_transfer` (updated)

- [ ] **Step 1: Replace network_task.h with updated declarations**

Replace the entire content of `client/network_task.h`:

```c
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
bool network_cmd_ls(void);

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
```

- [ ] **Step 2: Verify no compilation errors on header alone**

```bash
gcc -fsyntax-only -I. -I../lvgl client/network_task.h 2>&1 || true
```

Expected: header parses correctly (may need `-D_GNU_SOURCE` etc; the point is the structs/types are valid C)

- [ ] **Step 3: Commit**

```bash
git add client/network_task.h
git commit -m "feat: add transfer queue, thread pool, and credential types to network_task.h"
```

---

### Task 2: Implement queue operations and credential storage in network_task.c

**Files:**
- Modify: `client/network_task.c`

**Interfaces:**
- Consumes: `transfer_queue_t`, `transfer_task_t` from Task 1
- Produces: `queue_init`, `queue_push`, `queue_pop`, `queue_cancel_all` implementations; `g_login_ip/port/user/pass` storage

- [ ] **Step 1: Add global credential and queue definitions**

At the top of `network_task.c`, after the existing global definitions (`g_session_info[64] = {0};`), add:

```c
/* Saved login credentials for transfer workers */
char g_login_ip[64]   = {0};
char g_login_port[16] = {0};
char g_login_user[64] = {0};
char g_login_pass[64] = {0};

/* Transfer queue and thread pool */
transfer_queue_t  g_tx_queue;
transfer_worker_t g_tx_workers[TRANSFER_POOL_SIZE];
volatile int      g_active_transfers = 0;
```

- [ ] **Step 2: Add queue operation implementations**

After the existing helper functions (`put_le32`, `get_le32`, etc.), add:

```c
/* ================================================================== */
/*  Transfer queue operations (thread-safe)                           */
/* ================================================================== */

static void queue_init(transfer_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void queue_destroy(transfer_queue_t *q)
{
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static bool queue_push(transfer_queue_t *q, const transfer_task_t *task)
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

/* Returns false if cancelled and queue empty (worker should exit). */
static bool queue_pop(transfer_queue_t *q, transfer_task_t *task)
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

static void queue_cancel_all(transfer_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    q->cancelled = true;
    pthread_cond_broadcast(&q->cond);
    /* drain remaining tasks */
    q->head = q->tail = q->count = 0;
    pthread_mutex_unlock(&q->mutex);
}
```

- [ ] **Step 3: Save credentials in network_thread_func**

In `network_thread_func()`, after the existing credential copy loop (the block that copies `ip`, `port`, `username`, `password` from `params`), add global credential storage:

```c
    /* save credentials globally for transfer workers */
    strncpy(g_login_ip,   ip,       sizeof(g_login_ip) - 1);
    strncpy(g_login_port, port,     sizeof(g_login_port) - 1);
    strncpy(g_login_user, username, sizeof(g_login_user) - 1);
    strncpy(g_login_pass, password, sizeof(g_login_pass) - 1);
```

- [ ] **Step 4: Initialize the transfer pool after successful login**

In `network_thread_func()`, after the login success handling (after `lv_async_call(cb_login_result, ...)` and `free(rsp)`, but before the main RX loop), add:

```c
    /* initialize transfer pool after successful login */
    transfer_pool_init();
```

- [ ] **Step 5: Stop pool on disconnect**

In `network_thread_func()`, in the cleanup section at the end (before `g_login_ok = false`), add:

```c
    transfer_pool_stop();
```

- [ ] **Step 6: Commit**

```bash
git add client/network_task.c
git commit -m "feat: add queue operations, credential storage, and pool lifecycle hooks"
```

---

### Task 3: Implement transfer worker thread and per-task download logic

**Files:**
- Modify: `client/network_task.c`

**Interfaces:**
- Consumes: `transfer_task_t`, `transfer_queue_t`, `transfer_worker_t`, queue ops, `g_login_*` from Tasks 1-2
- Produces: `transfer_pool_init`, `transfer_pool_stop`, `transfer_worker_func`, `do_download_task`, `worker_login`, progress/complete/error callback helpers for transfer tasks

- [ ] **Step 1: Add forward declarations and async-callback data types for transfer tasks**

Before the existing async-callback section (near `make_str_data`), add:

```c
/* ------------------------------------------------------------------ */
/*  Transfer-task async callback types                                */
/* ------------------------------------------------------------------ */

/* Per-task progress data */
typedef struct {
    char filename[256];
    int  percent;
    int  current_bytes;
    int  total_bytes;
    bool is_upload;
} tx_progress_data_t;

/* Per-task completion data */
typedef struct {
    char filename[256];
    bool success;
    bool is_upload;
} tx_done_data_t;

/* Forward declarations for transfer worker helpers */
static bool worker_login(int sock);
static void do_download_task(const transfer_task_t *task);
static void do_upload_task(const transfer_task_t *task);
static void report_tx_progress(const char *filename, int pct,
                                int cur, int total, bool is_upload);
static void report_tx_complete(const char *filename, bool success,
                                bool is_upload);
static void report_all_done(void);
```

- [ ] **Step 2: Add async callback implementations for transfer progress/complete**

After the existing `cb_ul_done` function, add:

```c
/* ---- Transfer-task async callbacks (called on UI thread) ---- */

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
    bool any_active = (g_active_transfers > 0 || g_tx_queue.count > 0);
    if (!any_active) {
        ui_hide_progress();
        ui_set_status(g_tx_queue.cancelled ? "Transfer cancelled" : "Transfer complete");
        ui_restore_status_after_delay();
    }
}
```

- [ ] **Step 3: Implement report helpers (called from worker threads)**

```c
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
```

- [ ] **Step 4: Implement worker_login**

```c
/* Login helper for transfer workers (independent socket).
 * Returns true on successful login. */
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

    /* read login response */
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
```

- [ ] **Step 5: Implement do_download_task**

```c
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

    /* send GET command */
    int len;
    unsigned char *pkt = build_cmd_with_str(FTP_CMD_GET, task->filename, &len);
    if (!pkt) { close(sock); report_tx_complete(task->filename, false, false); return; }
    write(sock, pkt, (size_t)len);
    free(pkt);

    /* read GET response (expect filesize in LE) */
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

    /* create local file in client/load/ */
    mkdir("./load", 0755);
    char path[520];
    snprintf(path, sizeof(path), "./load/%s", task->filename);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        close(sock);
        report_tx_complete(task->filename, false, false);
        return;
    }

    /* receive raw file data */
    unsigned char buf[4096];
    int received  = 0;
    int last_pct  = -1;

    while (received < filesize) {
        if (g_tx_queue.cancelled) break;

        int remaining = filesize - received;
        int to_read = (remaining < 4096) ? remaining : 4096;
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
```

- [ ] **Step 6: Commit**

```bash
git add client/network_task.c
git commit -m "feat: add transfer worker thread, worker_login, and download task"
```

---

### Task 4: Implement upload task and transfer pool lifecycle

**Files:**
- Modify: `client/network_task.c`

**Interfaces:**
- Consumes: `do_download_task`, `worker_login`, `report_*` helpers from Task 3
- Produces: `do_upload_task`, `transfer_worker_func`, `transfer_pool_init`, `transfer_pool_stop`

- [ ] **Step 1: Implement do_upload_task**

Add after `do_download_task`:

```c
static void do_upload_task(const transfer_task_t *task)
{
    /* stat the local file in client/ directory */
    struct stat st;
    if (stat(task->filename, &st) != 0) {
        report_tx_complete(task->filename, false, true);
        return;
    }
    int filesize = (int)st.st_size;

    /* open local file */
    int local_fd = open(task->filename, O_RDONLY);
    if (local_fd < 0) {
        report_tx_complete(task->filename, false, true);
        return;
    }

    /* connect to server */
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

    /* send PUT command */
    /* build_cmd_put: [4B name_len][filename][4B filesize] */
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

    /* read PUT ack */
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

    /* send raw file data */
    unsigned char buf[4096];
    int sent     = 0;
    int last_pct = -1;

    while (sent < filesize) {
        if (g_tx_queue.cancelled) break;

        int remaining = filesize - sent;
        int to_read = (remaining < 4096) ? remaining : 4096;
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
```

- [ ] **Step 2: Implement transfer_worker_func**

```c
static void *transfer_worker_func(void *arg)
{
    transfer_worker_t *worker = (transfer_worker_t *)arg;
    transfer_task_t task;

    while (worker->running) {
        if (!queue_pop(&g_tx_queue, &task))
            break;  /* cancelled and queue empty */

        __sync_fetch_and_add(&g_active_transfers, 1);

        if (task.is_upload) {
            do_upload_task(&task);
        } else {
            do_download_task(&task);
        }

        __sync_fetch_and_sub(&g_active_transfers, 1);
    }

    /* check if all transfers are done */
    if (g_active_transfers == 0 && g_tx_queue.count == 0) {
        report_all_done();
    }
    return NULL;
}
```

- [ ] **Step 3: Implement transfer_pool_init and transfer_pool_stop**

```c
void transfer_pool_init(void)
{
    queue_init(&g_tx_queue);

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
    queue_cancel_all(&g_tx_queue);

    for (int i = 0; i < TRANSFER_POOL_SIZE; i++) {
        g_tx_workers[i].running = false;
    }
    /* wake any workers still waiting */
    pthread_cond_broadcast(&g_tx_queue.cond);

    /* brief pause to let workers exit */
    usleep(100000);

    queue_destroy(&g_tx_queue);
    g_active_transfers = 0;
}
```

- [ ] **Step 4: Commit**

```bash
git add client/network_task.c
git commit -m "feat: add upload task, worker func, and pool lifecycle"
```

---

### Task 5: Implement public API functions and update network_cancel_transfer

**Files:**
- Modify: `client/network_task.c`

**Interfaces:**
- Consumes: `g_tx_queue`, `g_active_transfers`, queue ops, `transfer_pool_*` from Tasks 2-4
- Produces: `network_cmd_get_multi`, `network_cmd_put_multi`, updated `network_cancel_transfer`

- [ ] **Step 1: Add network_cmd_get_multi implementation**

Replace the existing `network_cmd_get` function with `network_cmd_get_multi`:

```c
bool network_cmd_get_multi(const char **filenames, int count)
{
    if (!g_network_running || !filenames || count <= 0) return false;
    if (count > MAX_SELECTED_FILES) count = MAX_SELECTED_FILES;

    /* show progress popup once for the batch */
    lv_async_call(cb_show_progress_batch, NULL);

    int enqueued = 0;
    for (int i = 0; i < count; i++) {
        if (!filenames[i] || strlen(filenames[i]) == 0) continue;

        transfer_task_t task;
        memset(&task, 0, sizeof(task));
        strncpy(task.filename, filenames[i], sizeof(task.filename) - 1);
        task.is_upload = false;

        if (queue_push(&g_tx_queue, &task)) {
            enqueued++;
        }
    }

    return enqueued > 0;
}
```

Keep the old `network_cmd_get` as a compatibility wrapper (optional, but avoids breaking if referenced):

```c
bool network_cmd_get(const char *filename)
{
    return network_cmd_get_multi(&filename, 1);
}
```

- [ ] **Step 2: Add network_cmd_put_multi implementation**

Replace the existing `network_cmd_put` function:

```c
bool network_cmd_put_multi(const char **filenames, int count)
{
    if (!g_network_running || !filenames || count <= 0) return false;
    if (count > MAX_SELECTED_FILES) count = MAX_SELECTED_FILES;

    /* validate existence: if any file doesn't exist, show popup but continue with valid ones */
    int valid_count = 0;
    const char *valid_files[MAX_SELECTED_FILES];
    struct stat st;

    for (int i = 0; i < count; i++) {
        if (!filenames[i] || strlen(filenames[i]) == 0) continue;
        if (stat(filenames[i], &st) != 0) {
            /* file doesn't exist - show error popup on UI thread */
            str_data_t *err = make_str_data("file unexist");
            if (err) {
                /* we need a dedicated callback for error popup */
                lv_async_call(cb_show_error_popup, err);
            }
            continue;
        }
        valid_files[valid_count++] = filenames[i];
    }

    if (valid_count == 0) return false;

    /* show progress popup once for the batch */
    lv_async_call(cb_show_progress_batch, NULL);

    int enqueued = 0;
    for (int i = 0; i < valid_count; i++) {
        transfer_task_t task;
        memset(&task, 0, sizeof(task));
        strncpy(task.filename, valid_files[i], sizeof(task.filename) - 1);
        task.is_upload = true;

        if (queue_push(&g_tx_queue, &task)) {
            enqueued++;
        }
    }

    return enqueued > 0;
}
```

Keep the old `network_cmd_put` as a compatibility wrapper:

```c
bool network_cmd_put(const char *filename)
{
    return network_cmd_put_multi(&filename, 1);
}
```

- [ ] **Step 3: Update network_cancel_transfer**

Replace existing `network_cancel_transfer`:

```c
void network_cancel_transfer(void)
{
    queue_cancel_all(&g_tx_queue);
    g_active_transfers = 0;
}
```

- [ ] **Step 4: Add cb_show_progress_batch and cb_show_error_popup async callbacks**

```c
static void cb_show_progress_batch(void *data)
{
    (void)data;
    ui_show_progress_batch();
}

static void cb_show_error_popup(void *data)
{
    str_data_t *d = (str_data_t *)data;
    ui_show_error_popup(d ? d->text : "file unexist");
    free(d);
}
```

- [ ] **Step 5: Commit**

```bash
git add client/network_task.c
git commit -m "feat: add multi-file GET/PUT API, updated cancel, batch progress"
```

---

### Task 6: Update ui_manager.h with multi-select and transfer UI declarations

**Files:**
- Modify: `client/ui_manager.h`

**Interfaces:**
- Produces: `g_selected_remote`, `g_remote_sel_count`, `g_selected_local`, `g_local_sel_count`, `ui_update_transfer_progress`, `ui_on_transfer_done`, `ui_show_progress_batch`, `ui_update_file_list_cb` (unchanged), `ui_refresh_local_files` (updated)

- [ ] **Step 1: Replace ui_manager.h**

```c
/**
 * @file ui_manager.h
 * @brief LVGL UI Manager for FTP client
 *
 * Manages three screens: login, main (file list with multi-select),
 * and a progress overlay.  All LVGL widget operations run on the UI
 * thread; the network thread pushes data through lv_async_call().
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "../lvgl/lvgl.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */
#define MAX_SELECTED_FILES  128

/* ------------------------------------------------------------------ */
/*  Screen references                                                 */
/* ------------------------------------------------------------------ */
extern lv_obj_t *login_screen;
extern lv_obj_t *main_screen;

/* ------------------------------------------------------------------ */
/*  Multi-selection state                                             */
/* ------------------------------------------------------------------ */
extern char g_selected_remote[MAX_SELECTED_FILES][256];
extern int  g_remote_sel_count;
extern char g_selected_local[MAX_SELECTED_FILES][256];
extern int  g_local_sel_count;

/* ------------------------------------------------------------------ */
/*  Initialisation (call once from main())                            */
/* ------------------------------------------------------------------ */
void ui_login_init(void);
void ui_main_init(void);

/* ------------------------------------------------------------------ */
/*  Screen switching                                                  */
/* ------------------------------------------------------------------ */
void ui_switch_to_login(void);
void ui_switch_to_main(void);

/* ------------------------------------------------------------------ */
/*  Progress overlay                                                  */
/* ------------------------------------------------------------------ */
void ui_show_progress(const char *filename, bool is_upload);
void ui_show_progress_batch(void);   /* multi-file progress panel */
void ui_hide_progress(void);
void ui_update_progress(int percent, int current_bytes, int total_bytes,
                        const char *filename, bool is_upload);

/* Per-transfer progress update (called from transfer workers) */
void ui_update_transfer_progress(const char *filename, int percent,
                                  int current_bytes, int total_bytes,
                                  bool is_upload);

/* Per-transfer completion callback */
void ui_on_transfer_done(const char *filename, bool success, bool is_upload);

/* ------------------------------------------------------------------ */
/*  Status & error                                                    */
/* ------------------------------------------------------------------ */
void ui_set_status(const char *msg);
void ui_show_error(const char *msg);

/* ------------------------------------------------------------------ */
/*  Async callbacks (called from network thread via lv_async_call)    */
/* ------------------------------------------------------------------ */
void ui_update_file_list_cb(void *data);   /* data = malloc'd char* */

/* async callback: update local file list from scan result */
void ui_update_local_file_list_cb(void *data);

/* scan local working directory and refresh the local file list via async call */
void ui_refresh_local_files(void);

/* restore status bar to connected state after a short delay */
void ui_restore_status_after_delay(void);

/* show a centered popup with a Close button (for errors like "file unexist") */
void ui_show_error_popup(const char *msg);

#endif /* UI_MANAGER_H */
```

- [ ] **Step 2: Commit**

```bash
git add client/ui_manager.h
git commit -m "feat: update ui_manager.h with multi-select and transfer UI declarations"
```

---

### Task 7: Implement multi-select for remote and local file lists in ui_manager.c

**Files:**
- Modify: `client/ui_manager.c`

**Interfaces:**
- Consumes: `g_selected_remote/local`, `MAX_SELECTED_FILES` from Task 6; existing `style_selected_remote/local`, list objects
- Produces: Multi-select toggle logic for both lists, updated `ui_refresh_local_files` scanning `client/` dir

- [ ] **Step 1: Add global multi-select state definitions**

After the existing `#define SIZE 4096`, add:

```c
/* Multi-selection state */
char g_selected_remote[MAX_SELECTED_FILES][256] = {{0}};
int  g_remote_sel_count = 0;
char g_selected_local[MAX_SELECTED_FILES][256]  = {{0}};
int  g_local_sel_count  = 0;
```

- [ ] **Step 2: Replace on_file_item_clicked (remote list) with multi-select toggle**

Replace the existing `on_file_item_clicked` function:

```c
static void on_file_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn || !main_file_list) return;

    const char *text = lv_list_get_button_text(main_file_list, btn);
    if (!text) return;

    /* check if already selected (toggle off) */
    for (int i = 0; i < g_remote_sel_count; i++) {
        if (strcmp(g_selected_remote[i], text) == 0) {
            /* deselect: remove green, shift array */
            lv_obj_remove_style(btn, &style_selected_remote, 0);
            for (int j = i; j < g_remote_sel_count - 1; j++) {
                strncpy(g_selected_remote[j], g_selected_remote[j + 1],
                        sizeof(g_selected_remote[j]) - 1);
            }
            g_remote_sel_count--;
            goto update_label;
        }
    }

    /* not yet selected (toggle on) */
    if (g_remote_sel_count >= MAX_SELECTED_FILES) return; /* full */

    lv_obj_add_style(btn, &style_selected_remote, 0);
    strncpy(g_selected_remote[g_remote_sel_count], text,
            sizeof(g_selected_remote[g_remote_sel_count]) - 1);
    g_remote_sel_count++;

update_label:
    /* merge label: show both remote and local counts */
    if (main_selected_label) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Remote: %d selected | Local: %d selected",
                 g_remote_sel_count, g_local_sel_count);
        lv_label_set_text(main_selected_label, buf);
    }
}
```

- [ ] **Step 3: Replace on_local_file_item_clicked with multi-select toggle**

Replace the existing `on_local_file_item_clicked` function:

```c
static void on_local_file_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn || !main_local_list) return;

    const char *text = lv_list_get_button_text(main_local_list, btn);
    if (!text) return;

    /* check if already selected (toggle off) */
    for (int i = 0; i < g_local_sel_count; i++) {
        if (strcmp(g_selected_local[i], text) == 0) {
            lv_obj_remove_style(btn, &style_selected_local, 0);
            for (int j = i; j < g_local_sel_count - 1; j++) {
                strncpy(g_selected_local[j], g_selected_local[j + 1],
                        sizeof(g_selected_local[j]) - 1);
            }
            g_local_sel_count--;
            goto update_label;
        }
    }

    /* toggle on */
    if (g_local_sel_count >= MAX_SELECTED_FILES) return;

    lv_obj_add_style(btn, &style_selected_local, 0);
    strncpy(g_selected_local[g_local_sel_count], text,
            sizeof(g_selected_local[g_local_sel_count]) - 1);
    g_local_sel_count++;

update_label:
    if (main_selected_label) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Remote: %d selected | Local: %d selected",
                 g_remote_sel_count, g_local_sel_count);
        lv_label_set_text(main_selected_label, buf);
    }
}
```

- [ ] **Step 4: Update scan_local_directory to scan client/ dir excluding subdirectories**

Replace the existing `scan_local_directory` function:

```c
static char *scan_local_directory(void)
{
    char *buf = (char *)malloc(SIZE);
    if (!buf) return NULL;
    int off = 0;
    buf[0] = '\0';

    /* scan client/ directory (current working directory), files only */
    DIR *dir = opendir(".");
    if (!dir) {
        strncpy(buf, "(cannot open cwd)", SIZE - 1);
        return buf;
    }

    struct dirent *d;
    while ((d = readdir(dir)) != NULL) {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
            continue;

        /* skip directories */
        struct stat st;
        if (stat(d->d_name, &st) == 0 && S_ISDIR(st.st_mode))
            continue;

        off += snprintf(buf + off, (size_t)(SIZE - off - 1),
                        "%s\n", d->d_name);
    }
    closedir(dir);
    return buf;
}
```

- [ ] **Step 5: Clear multi-select state when file lists are refreshed**

In `ui_update_file_list_cb`, after `lv_obj_clean(main_file_list)`, add:

```c
    /* clear remote selection state */
    g_remote_sel_count = 0;
    memset(g_selected_remote, 0, sizeof(g_selected_remote));
```

In `ui_update_local_file_list_cb`, after `lv_obj_clean(main_local_list)`, add:

```c
    /* clear local selection state */
    g_local_sel_count = 0;
    memset(g_selected_local, 0, sizeof(g_selected_local));
```

And update the merged label after both:

```c
    if (main_selected_label) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Remote: %d selected | Local: %d selected",
                 g_remote_sel_count, g_local_sel_count);
        lv_label_set_text(main_selected_label, buf);
    }
```

Also remove the old `g_selected_file[0] = '\0';` and `g_local_selected_file[0] = '\0';` lines since those single-file variables are no longer used.

- [ ] **Step 6: Commit**

```bash
git add client/ui_manager.c
git commit -m "feat: add multi-select toggle for remote and local file lists"
```

---

### Task 8: Update button layout and wiring in ui_manager.c

**Files:**
- Modify: `client/ui_manager.c`

**Interfaces:**
- Consumes: Multi-select state from Task 7; `network_cmd_get_multi`, `network_cmd_put_multi` from Task 5
- Produces: New single-row button layout, updated Download/Upload/Refresh handlers

- [ ] **Step 1: Update button row in ui_main_init**

Replace the bottom button row and upload row in `ui_main_init` (from the `/* ======== action buttons row ======== */` section through the upload row section):

```c
    /* ======== selected file display ======== */
    main_selected_label = lv_label_create(main_screen);
    lv_obj_align(main_selected_label, LV_ALIGN_BOTTOM_LEFT, 10, -48);
    lv_obj_set_style_text_color(main_selected_label, lv_color_hex(0x88ccff), 0);
    lv_label_set_text(main_selected_label, "Remote: 0 selected | Local: 0 selected");

    /* ======== action buttons row (single row) ======== */
    lv_obj_t *btn_row = lv_obj_create(main_screen);
    lv_obj_set_size(btn_row, LV_PCT(100), 46);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_row, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_left(btn_row, 4, 0);
    lv_obj_set_style_pad_right(btn_row, 4, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_btn(btn_row, "Refresh",    90, 34, on_refresh_btn_clicked);
    create_btn(btn_row, "Download",   90, 34, on_download_btn_clicked);
    create_btn(btn_row, "Upload",     90, 34, on_upload_btn_clicked);
    create_btn(btn_row, "Disconnect", 100, 34, on_disconnect_btn_clicked);
```

Also remove the `main_upload_ta` and `main_upload_btn` static variable declarations at the top of the file since we no longer need them.

- [ ] **Step 2: Update on_download_btn_clicked**

Replace the existing `on_download_btn_clicked`:

```c
static void on_download_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (g_active_transfers > 0) {
        ui_show_error("Transfer already in progress");
        return;
    }
    if (g_remote_sel_count == 0) {
        ui_show_error("No file selected");
        return;
    }

    /* build filename array */
    const char *files[MAX_SELECTED_FILES];
    for (int i = 0; i < g_remote_sel_count; i++) {
        files[i] = g_selected_remote[i];
    }

    ui_set_status("Downloading...");
    if (!network_cmd_get_multi(files, g_remote_sel_count)) {
        ui_show_error("Failed to start download");
    }
}
```

- [ ] **Step 3: Update on_upload_btn_clicked**

Replace the existing `on_upload_btn_clicked`:

```c
static void on_upload_btn_clicked(lv_event_t *e)
{
    (void)e;
    if (g_active_transfers > 0) {
        ui_show_error("Transfer already in progress");
        return;
    }
    if (g_local_sel_count == 0) {
        ui_show_error("No file selected");
        return;
    }

    /* build filename array */
    const char *files[MAX_SELECTED_FILES];
    for (int i = 0; i < g_local_sel_count; i++) {
        files[i] = g_selected_local[i];
    }

    ui_set_status("Uploading...");
    if (!network_cmd_put_multi(files, g_local_sel_count)) {
        /* network_cmd_put_multi shows "file unexist" popup(s) internally
         * for missing files. If it returns false, all were invalid. */
        ui_show_error("No valid files to upload");
    }
}
```

- [ ] **Step 4: Update on_refresh_btn_clicked to restore status immediately**

```c
static void on_refresh_btn_clicked(lv_event_t *e)
{
    (void)e;
    ui_set_status("Refreshing...");
    network_cmd_ls();
    ui_refresh_local_files();
    /* restore status immediately (don't wait for the 3s timer) */
    if (g_login_ok) {
        char sb[128];
        snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
                 g_login_ip[0] ? g_login_user : "admin",
                 g_session_info);
        ui_set_status(sb);
    }
}
```

Note: this accesses `g_login_user` and `g_session_info` which are global. The `g_login_user` comes from network_task.h globals.

- [ ] **Step 5: Remove unused static variables**

Remove from the static variable declarations:
- `static lv_obj_t *main_upload_ta;` — no longer used
- `static lv_obj_t *main_upload_btn;` — no longer used
- `static char g_selected_file[256] = {0};` — replaced by multi-select array
- `static char g_local_selected_file[256] = {0};` — replaced by multi-select array
- `static lv_obj_t *g_prev_remote_btn = NULL;` — no longer used (multi-select tracks by name, not pointer)
- `static lv_obj_t *g_prev_local_btn = NULL;` — no longer used

- [ ] **Step 6: Commit**

```bash
git add client/ui_manager.c
git commit -m "feat: update button layout, wire Download/Upload/Refresh to multi-file API"
```

---

### Task 9: Implement multi-transfer progress popup and status bar lifecycle

**Files:**
- Modify: `client/ui_manager.c`

**Interfaces:**
- Consumes: `ui_show_progress_batch` declaration from Task 6; existing overlay infrastructure
- Produces: `ui_show_progress_batch`, `ui_update_transfer_progress`, `ui_on_transfer_done`, status bar lifecycle

- [ ] **Step 1: Add progress tracking structures for multi-transfer**

At the top of the file (after the existing static variables), add:

```c
/* ---- multi-transfer progress tracking ---- */
#define MAX_PROGRESS_BARS 10

typedef struct {
    lv_obj_t *bar;
    lv_obj_t *label;
    char      filename[256];
    bool      is_upload;
    bool      active;
    bool      done;
} prog_slot_t;

static prog_slot_t prog_slots[MAX_PROGRESS_BARS];
static lv_obj_t   *batch_prog_panel = NULL;
static int          batch_prog_count = 0;
```

- [ ] **Step 2: Implement ui_show_progress_batch**

Add before the existing `ui_show_progress`:

```c
void ui_show_progress_batch(void)
{
    lv_obj_t *parent = lv_layer_top();
    if (!parent) return;

    /* if panel already exists, reuse it */
    if (batch_prog_panel) {
        /* reset all slots */
        for (int i = 0; i < MAX_PROGRESS_BARS; i++) {
            prog_slots[i].active = false;
            prog_slots[i].done   = false;
        }
        batch_prog_count = 0;
        return;
    }

    memset(prog_slots, 0, sizeof(prog_slots));

    batch_prog_panel = lv_obj_create(parent);
    lv_obj_set_size(batch_prog_panel, 380, 300);
    lv_obj_center(batch_prog_panel);
    lv_obj_set_style_bg_color(batch_prog_panel, lv_color_hex(0x222244), 0);
    lv_obj_set_style_border_color(batch_prog_panel, lv_color_hex(0x4488cc), 0);
    lv_obj_set_style_border_width(batch_prog_panel, 2, 0);
    lv_obj_set_style_radius(batch_prog_panel, 10, 0);
    lv_obj_set_style_pad_all(batch_prog_panel, 8, 0);
    lv_obj_set_scrollbar_mode(batch_prog_panel, LV_SCROLLBAR_MODE_AUTO);

    /* title */
    lv_obj_t *title = lv_label_create(batch_prog_panel);
    lv_label_set_text(title, "Transfer Progress");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    /* Close button */
    lv_obj_t *close_btn = lv_button_create(batch_prog_panel);
    lv_obj_set_size(close_btn, 70, 26);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_t *cbl = lv_label_create(close_btn);
    lv_label_set_text(cbl, "Close");
    lv_obj_center(cbl);
    lv_obj_add_event_cb(close_btn, on_close_progress_btn_clicked, LV_EVENT_CLICKED, NULL);

    /* Cancel button */
    lv_obj_t *cancel_btn = lv_button_create(batch_prog_panel);
    lv_obj_set_size(cancel_btn, 80, 26);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_t *cbl2 = lv_label_create(cancel_btn);
    lv_label_set_text(cbl2, "Cancel All");
    lv_obj_center(cbl2);
    lv_obj_add_event_cb(cancel_btn, on_cancel_btn_clicked, LV_EVENT_CLICKED, NULL);

    /* container for progress bars */
    lv_obj_t *cont = lv_obj_create(batch_prog_panel);
    lv_obj_set_size(cont, LV_PCT(100), 220);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    /* pre-create progress bar slots */
    for (int i = 0; i < MAX_PROGRESS_BARS; i++) {
        prog_slots[i].active = false;
        prog_slots[i].done   = false;

        /* container per slot */
        lv_obj_t *slot = lv_obj_create(cont);
        lv_obj_set_size(slot, LV_PCT(95), 36);
        lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(slot, 0, 0);
        lv_obj_set_style_pad_all(slot, 0, 0);
        lv_obj_add_flag(slot, LV_OBJ_FLAG_HIDDEN);

        prog_slots[i].label = lv_label_create(slot);
        lv_label_set_text(prog_slots[i].label, "");
        lv_obj_set_style_text_color(prog_slots[i].label, lv_color_hex(0xcccccc), 0);
        lv_obj_align(prog_slots[i].label, LV_ALIGN_TOP_LEFT, 0, 0);

        prog_slots[i].bar = lv_bar_create(slot);
        lv_obj_set_size(prog_slots[i].bar, LV_PCT(100), 14);
        lv_obj_align(prog_slots[i].bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_bar_set_range(prog_slots[i].bar, 0, 100);

        /* store slot obj for show/hide */
        lv_obj_set_user_data(slot, (void *)(intptr_t)i);
    }

    batch_prog_count = 0;
}
```

- [ ] **Step 3: Implement ui_update_transfer_progress**

```c
void ui_update_transfer_progress(const char *filename, int percent,
                                  int current_bytes, int total_bytes,
                                  bool is_upload)
{
    if (!batch_prog_panel) return;

    /* find existing slot or allocate new one */
    int slot_idx = -1;
    for (int i = 0; i < MAX_PROGRESS_BARS; i++) {
        if (prog_slots[i].active &&
            strcmp(prog_slots[i].filename, filename) == 0 &&
            prog_slots[i].is_upload == is_upload) {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx < 0) {
        /* find first inactive slot */
        for (int i = 0; i < MAX_PROGRESS_BARS; i++) {
            if (!prog_slots[i].active && !prog_slots[i].done) {
                slot_idx = i;
                break;
            }
        }
    }

    if (slot_idx < 0 || slot_idx >= MAX_PROGRESS_BARS) return;

    /* activate slot */
    if (!prog_slots[slot_idx].active) {
        prog_slots[slot_idx].active = true;
        strncpy(prog_slots[slot_idx].filename, filename,
                sizeof(prog_slots[slot_idx].filename) - 1);
        prog_slots[slot_idx].is_upload = is_upload;
        batch_prog_count++;

        /* show the slot container */
        lv_obj_t *cont = batch_prog_panel;
        /* get the flex container (child at index 3 after title, close, cancel) */
        lv_obj_t *flex_cont = lv_obj_get_child(cont, 3);
        if (flex_cont) {
            lv_obj_t *slot_obj = lv_obj_get_child(flex_cont, (uint32_t)slot_idx);
            if (slot_obj) lv_obj_clear_flag(slot_obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* update bar and label */
    lv_bar_set_value(prog_slots[slot_idx].bar, percent, LV_ANIM_ON);

    char buf[256];
    char size_str[32];
    if (total_bytes >= 1048576)
        snprintf(size_str, sizeof(size_str), "%.1f/%.1f MB",
                 current_bytes / 1048576.0f, total_bytes / 1048576.0f);
    else if (total_bytes >= 1024)
        snprintf(size_str, sizeof(size_str), "%.1f/%.1f KB",
                 current_bytes / 1024.0f, total_bytes / 1024.0f);
    else
        snprintf(size_str, sizeof(size_str), "%d/%d B", current_bytes, total_bytes);

    snprintf(buf, sizeof(buf), "[%s] %s %d%% %s",
             is_upload ? "UP" : "DL", filename, percent, size_str);
    lv_label_set_text(prog_slots[slot_idx].label, buf);

    /* update global progress too (for existing single-file overlay compatibility) */
    ui_update_progress(percent, current_bytes, total_bytes, filename, is_upload);
}
```

- [ ] **Step 4: Implement ui_on_transfer_done**

```c
void ui_on_transfer_done(const char *filename, bool success, bool is_upload)
{
    if (!batch_prog_panel) return;

    /* find and mark slot as done */
    for (int i = 0; i < MAX_PROGRESS_BARS; i++) {
        if (prog_slots[i].active &&
            strcmp(prog_slots[i].filename, filename) == 0 &&
            prog_slots[i].is_upload == is_upload) {
            prog_slots[i].active = false;
            prog_slots[i].done   = true;
            batch_prog_count--;

            /* update label to show result */
            char buf[256];
            snprintf(buf, sizeof(buf), "[%s] %s - %s",
                     is_upload ? "UP" : "DL", filename,
                     success ? "OK" : "FAILED");
            lv_label_set_text(prog_slots[i].label, buf);
            lv_obj_set_style_text_color(prog_slots[i].label,
                success ? lv_color_hex(0x88ff88) : lv_color_hex(0xff4444), 0);
            lv_bar_set_value(prog_slots[i].bar, success ? 100 : 0, LV_ANIM_OFF);
            break;
        }
    }

    /* if no more active transfers, auto-hide after short delay */
    if (batch_prog_count <= 0 && g_active_transfers == 0) {
        /* brief delay then hide */
        /* handled by cb_all_done which calls ui_hide_progress */
    }
}
```

- [ ] **Step 5: Update ui_hide_progress to handle batch panel**

Replace the existing `ui_hide_progress`:

```c
void ui_hide_progress(void)
{
    /* hide batch panel */
    if (batch_prog_panel) {
        lv_obj_del(batch_prog_panel);
        batch_prog_panel = NULL;
        memset(prog_slots, 0, sizeof(prog_slots));
        batch_prog_count = 0;
    }

    /* hide single-file overlay */
    if (prog_panel) {
        lv_obj_del(prog_panel);
        prog_panel = NULL;
    }
    prog_label = NULL;
    prog_bar   = NULL;
    prog_info  = NULL;
}
```

- [ ] **Step 6: Update on_close_progress_btn_clicked**

Replace the existing handler:

```c
static void on_close_progress_btn_clicked(lv_event_t *e)
{
    (void)e;
    /* hide panels, transfers continue in background */
    if (batch_prog_panel) {
        lv_obj_del(batch_prog_panel);
        batch_prog_panel = NULL;
        memset(prog_slots, 0, sizeof(prog_slots));
        batch_prog_count = 0;
    }
    if (prog_panel) {
        lv_obj_del(prog_panel);
        prog_panel = NULL;
        prog_label = NULL;
        prog_bar   = NULL;
        prog_info  = NULL;
    }
}
```

- [ ] **Step 7: Commit**

```bash
git add client/ui_manager.c
git commit -m "feat: add multi-transfer progress popup and per-file tracking"
```

---

### Task 10: Update status bar lifecycle and restore timer

**Files:**
- Modify: `client/ui_manager.c`

**Interfaces:**
- Consumes: `g_login_ok`, `g_session_info`, `g_login_user` from network_task globals; existing `restore_status_timer_cb`
- Produces: Correct status bar transitions for all transfer states

- [ ] **Step 1: Update ui_set_status to track restore correctly**

The current `ui_set_status` is fine. The key is ensuring `ui_restore_status_after_delay` uses the right globals. Update `restore_status_timer_cb`:

```c
static void restore_status_timer_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    if (g_login_ok) {
        char sb[128];
        snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
                 g_login_user[0] ? g_login_user : "admin",
                 g_session_info[0] ? g_session_info : "N/A");
        ui_set_status(sb);
    }
}
```

- [ ] **Step 2: Update ui_switch_to_main status format**

In `ui_switch_to_main`, update the status bar snprintf to match the new format:

```c
    char sb[128];
    snprintf(sb, sizeof(sb), "User: %s  |  %s  |  Connected",
             lv_textarea_get_text(login_user_ta), g_session_info);
    if (main_status_bar) lv_label_set_text(main_status_bar, sb);
```

(Already correct, just verifying consistency.)

- [ ] **Step 3: Verify network globals are accessible**

`ui_manager.c` already includes `"network_task.h"`, which declares all needed network globals as `extern` (from Task 1). No additional extern declarations needed in ui_manager.c. The following globals are used in UI code and already declared in the header:

- `g_active_transfers` (`volatile int`)
- `g_login_ok` (`bool`)
- `g_session_info` (`char[64]`)
- `g_login_user` (`char[64]`)
- `g_login_ip` (`char[64]`)

- [ ] **Step 4: Commit**

```bash
git add client/ui_manager.c
git commit -m "feat: update status bar lifecycle and restore timer"
```

---

### Task 11: Clean up unused code and fix compilation

**Files:**
- Modify: `client/ui_manager.c`, `client/network_task.c`

**Interfaces:**
- Consumes: All previous tasks
- Produces: Clean compilation

- [ ] **Step 1: Remove unused static variables from ui_manager.c**

Remove these declarations from the top of `ui_manager.c`:
- `static lv_obj_t *main_upload_ta;` (if still present)
- `static lv_obj_t *main_upload_btn;` (if still present)
- `static char g_selected_file[256] = {0};`
- `static char g_local_selected_file[256] = {0};`
- `static lv_obj_t *g_prev_remote_btn = NULL;`
- `static lv_obj_t *g_prev_local_btn = NULL;`
- `static bool g_transferring = false;` — replaced by `g_active_transfers`

- [ ] **Step 2: Remove unused forward declarations from ui_manager.c**

Remove `on_local_file_item_clicked` from forward declarations if it was there (we're replacing it, not adding a new one). Actually keep it since it's used.

Check that `on_ta_focused` and `on_keyboard_event` are still needed (for login screen textareas — still used on login screen, keep them).

- [ ] **Step 3: Remove the old `g_transferring` checks**

Replace `g_transferring` checks with `g_active_transfers > 0` (done in Tasks 8 step 2 and 3).

- [ ] **Step 4: Verify compilation**

```bash
cd /mnt/hgfs/share2.0/Ubantudemo && make clean 2>&1; make 2>&1
```

Or if using the build script:

```bash
cd build && cmake .. && make 2>&1
```

Expected: Compilation succeeds without errors. If there are errors, fix them.

- [ ] **Step 5: Commit**

```bash
git add client/ui_manager.c client/network_task.c
git commit -m "chore: remove unused variables and fix compilation"
```

---

### Task 12: Final integration verification

**Files:**
- (No code changes — verification only)

- [ ] **Step 1: Start the server**

```bash
cd server && ./build/server 2>&1 &
```

- [ ] **Step 2: Start the client**

```bash
cd build && ./app 2>&1
```

- [ ] **Step 3: Verify login flow**
- Login with defaults (127.0.0.1:8888, admin/123456)
- Confirm main screen loads with remote file list and local file list
- Status bar shows "User: admin | SID-xxxxx | Connected"

- [ ] **Step 4: Verify multi-selection**
- Click remote files: each click toggles green highlight
- Click same file again: removes green
- Bottom label updates: "Remote: N selected | Local: M selected"
- Click local files: same toggle behavior

- [ ] **Step 5: Verify download**
- Select 1+ remote files (green)
- Click Download
- Progress popup appears with per-file progress bars
- Status bar shows "Downloading..."
- Files saved to `client/load/`
- After completion: status shows "Transfer complete" then auto-restores
- Click Refresh: local file list shows new files, status restores immediately

- [ ] **Step 6: Verify upload**
- Select 1+ local files (green)
- Click Upload
- Progress popup appears
- Status bar shows "Uploading..."
- Files saved to `server/copy/`
- After completion: status shows "Transfer complete" then auto-restores
- Click Refresh: remote file list shows new files

- [ ] **Step 7: Verify error handling**
- Upload non-existent file: "file unexist" popup with Close button
- Close popup: UI continues normally
- Disconnect during transfer: clean disconnect to login screen

- [ ] **Step 8: Commit (if any fixes were needed)**

```bash
git add -A
git commit -m "fix: integration fixes from verification"
```
