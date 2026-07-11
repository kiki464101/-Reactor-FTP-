# File Transfer Redesign — Multi-selection + Multi-threaded Transfer

**Date:** 2026-07-10
**Status:** Approved

---

## 1. Overview

Redesign the file transfer UI and network layer of the LVGL FTP client to support:

- **Multi-selection** of files (both remote and local) with green-highlight toggle
- **Batch Download**: selected remote files saved to `client/load/`
- **Batch Upload**: selected local files from `client/` saved to `server/copy/`
- **Multi-threaded transfer**: thread pool with independent socket connections per transfer
- **Progress popup**: per-file progress bar + percentage, closable without aborting transfer
- **Status bar lifecycle**: `Downloading...` / `Uploading...` → `Downloaded` / `Uploaded` → auto-restore after 3s
- **Refresh**: updates file lists after transfers, immediately restores status bar
- **Error popup**: "file unexist" with centered Close button when upload source missing

---

## 2. Architecture

```
┌──────────────────────────────────────────────────────┐
│                  UI Thread (LVGL)                      │
│  ┌─────────────────┐  ┌─────────────────┐             │
│  │ Remote File List │  │ Local File List │             │
│  │ multi-select     │  │ multi-select    │             │
│  │ green toggle     │  │ green toggle    │             │
│  └────────┬────────┘  └────────┬────────┘             │
│           │ Download           │ Upload               │
│           ▼                    ▼                      │
│  ┌─────────────────────────────────┐                  │
│  │       Progress Popup(s)         │                  │
│  │  独立进度条 + 百分比 + Close/Cancel │               │
│  └─────────────────────────────────┘                  │
│  Status Bar: Ready → Uploading/Downloading → Done     │
└──────────────┬───────────────────────────────────────┘
               │ lv_async_call (progress/status)
               ▼
┌──────────────────────────────────────────────────────┐
│           Main Network Thread                         │
│  - Login / LS / BYE (keeps main connection alive)    │
│  - DOES NOT handle file data transfer                │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│         Transfer Task Queue (thread-safe FIFO)        │
│  ┌────────┐ ┌────────┐ ┌────────┐                    │
│  │Task #1 │ │Task #2 │ │Task #3 │ ...                │
│  │GET a.txt│ │PUT b.c │ │GET c.d │                    │
│  └────────┘ └────────┘ └────────┘                    │
│  pthread_mutex + pthread_cond                         │
└──────────┬───────────────────────────────────────────┘
           ▼
┌──────────────────────────────────────────────────────┐
│         Transfer Thread Pool (N=3 workers)            │
│                                                       │
│  Each worker:                                         │
│  1. Dequeue task from shared queue                    │
│  2. Create independent TCP socket → connect to server │
│  3. Send LOGIN                                       │
│  4. Send GET/PUT command                              │
│  5. Transfer data (read/write raw bytes)              │
│  6. Report progress via lv_async_call                 │
│  7. Close connection                                  │
│  8. Repeat (dequeue next task, or sleep on cond)      │
│                                                       │
│  - Each worker has INDEPENDENT socket to server       │
│  - N workers = up to N concurrent transfers           │
│  - Cancel flag: g_cancel_all stops all workers        │
└──────────────────────────────────────────────────────┘
```

---

## 3. Multi-Selection (UI)

### 3.1 Remote file list

- Each file button click: toggle green highlight
- State tracking: `g_selected_remote[128][256]` array + `g_remote_sel_count` counter
- Click: if already in list → remove + restore normal color; else add + green color
- Bottom label: `"Remote: N selected | Local: M selected"` (merged label)

### 3.2 Local file list

- Same toggle mechanism as remote
- State tracking: `g_selected_local[128][256]` array + `g_local_sel_count` counter
- Scans `client/` directory (not `client/load/`), shows **files only** (excludes subdirectories)
- Style: `style_selected_local` (same green: `0x228B22`)

### 3.3 Selection style update

When a file list is refreshed (after LS response or local scan), previously selected items are **cleared**. This prevents stale selections pointing to files that no longer exist.

---

## 4. Download Flow

**Trigger:** User clicks Download with ≥1 remote files selected (green).

```
1. Validate: no selection → error "No file selected"
2. Validate: transfer in progress → error "Transfer already in progress"
3. Build transfer task list: {filename, is_upload=false} × N
4. Enqueue all tasks into transfer_queue
5. UI: show progress popup(s), status → "Downloading..."
6. Workers process tasks:
   a. connect → login → send GET → read size response
   b. receive raw data → write to client/load/<filename>
   c. periodic progress callback to UI
   d. task complete → worker dequeues next
7. All tasks done → status → "Downloaded"
8. 3-second timer → restore status bar to "User: admin | SID-xxxxx | Connected"
9. User clicks Refresh → remote LS + local scan, status restores immediately
```

**Save location:** `client/load/<filename>` (same as current)

---

## 5. Upload Flow

**Trigger:** User clicks Upload with ≥1 local files selected (green).

```
1. Validate: no selection → error "No file selected"
2. Validate: transfer in progress → error "Transfer already in progress"
3. Verify existence of each selected file under client/ directory
   - stat("./<filename>") → if missing, show "file unexist" popup, skip that file
4. Build transfer task list: {filename, is_upload=true} for valid files only
5. If all files invalid (0 valid) → return without starting
6. Enqueue valid tasks into transfer_queue
7. UI: show progress popup(s), status → "Uploading..."
8. Workers process tasks:
   a. connect → login → send PUT (filename + filesize) → read ack
   b. read local file from ./<filename> → send raw data
   c. periodic progress callback to UI
   d. task complete → worker dequeues next
9. All tasks done → status → "Uploaded"
10. 3-second timer → restore status bar
11. User clicks Refresh → remote LS + local scan, status restores immediately
```

**Upload source:** `client/<filename>` (the client project root, plain files only)
**Save location:** `server/copy/<filename>`

---

## 6. Transfer Thread Pool

### 6.1 Data Structures

```c
// Transfer task (in queue)
typedef struct {
    char    filename[256];
    bool    is_upload;
} transfer_task_t;

// Thread-safe queue
typedef struct {
    transfer_task_t tasks[256];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            cancelled;   // set to stop all workers
} transfer_queue_t;

// Worker thread state
typedef struct {
    pthread_t  thread;
    int        id;
    bool       running;
} transfer_worker_t;
```

### 6.2 Queue operations

- `queue_init()` — init mutex + cond
- `queue_push(task)` — add to tail, signal one worker
- `queue_pop(task*)` — block until task available or cancelled, returns false if cancelled
- `queue_cancel_all()` — set cancelled flag, broadcast all workers, drain queue
- `queue_destroy()` — cleanup

### 6.3 Pool lifecycle

- Pool created on first transfer after login (lazy init) or at `ui_main_init`
- N = 3 workers (configurable via `TRANSFER_POOL_SIZE`)
- Workers sleep on `pthread_cond_wait` when queue empty
- Pool destroyed on disconnect

### 6.4 Worker main loop

```c
void *transfer_worker_func(void *arg) {
    transfer_task_t task;
    while (worker->running) {
        if (!queue_pop(&g_tx_queue, &task))
            break;  // cancelled
        // execute the transfer
        do_transfer(&task);
    }
    return NULL;
}
```

### 6.5 Independent connection per transfer

Each `do_transfer` call:
1. `socket()` + `connect()` to server
2. Send LOGIN with credentials (stored from main login)
3. If upload: `build_cmd_put()` → send → read ack → send raw file data
4. If download: `build_cmd_with_str(GET)` → send → read size response → receive raw data → write to `client/load/`
5. Progress callback via `lv_async_call` to update the per-file progress popup
6. `close(sockfd)`

### 6.6 Cancel mechanism

- `network_cancel_transfer()` sets `g_tx_queue.cancelled = true`
- `pthread_cond_broadcast` wakes all workers
- Each worker: close current socket, delete partial file (if download), exit
- Queue drained

---

## 7. Progress Popup Enhancements

### 7.1 Multiple concurrent progress bars

When multiple files transfer simultaneously, the popup shows:
- A scrollable container with one progress bar per active transfer
- Each bar labeled: `[Downloading/Uploading] filename  XX%`
- Each bar shows: `current / total` in human-readable format

### 7.2 Buttons

- **Close**: hides the popup, transfers continue in background. Re-showable via a "Show Progress" button or auto-shown when new transfer starts.
- **Cancel All**: stops ALL transfers, closes popup.

### 7.3 Auto-dismiss

When all transfers complete, popup auto-hides after a short delay (1s).

---

## 8. Status Bar Lifecycle

| Event | Status Bar Text | Color |
|-------|----------------|-------|
| Normal (connected) | `User: admin \| SID-xxxxx \| Connected` | Cyan |
| Download start | `Downloading...` | Yellow |
| Upload start | `Uploading...` | Yellow |
| Download complete | `Downloaded` | Green |
| Upload complete | `Uploaded` | Green |
| Refresh clicked | `Refreshing...` | Yellow |
| Refresh done | Restore to normal | Cyan |
| Error | Error message text | Red |

**Restore timer:** After "Downloaded" / "Uploaded" shown, a 3-second LVGL timer fires `ui_restore_status_after_delay()` to restore the normal status bar text.

**Refresh special case:** When the user clicks Refresh, the status bar restores to normal immediately after the refresh completes (no 3s delay needed if a "Downloaded"/"Uploaded" message was showing).

---

## 9. Error Popup

- Centered popup with red border
- Message text centered
- One **Close** button centered at the bottom
- Clicking Close dismisses the popup
- Popup does NOT block other UI interactions
- Triggered when: upload file does not exist at `client/<filename>`

---

## 10. Button Layout (Main Screen)

Bottom row (replaces current two-row layout):
```
[  Refresh  ] [ Download  ] [  Upload   ] [ Disconnect ]
```

Removes:
- Upload textarea row (replaced by local multi-select)
- `main_selected_label` → replaced by merged `"Remote: N | Local: M"` label

---

## 11. Files Changed

| File | Changes |
|------|---------|
| `client/ui_manager.h` | Add multi-select APIs, updated progress callback signatures |
| `client/ui_manager.c` | Multi-select toggle logic, local file scan from `client/`, new button layout, updated popup, status bar lifecycle |
| `client/network_task.h` | Transfer queue + thread pool types, updated public API |
| `client/network_task.c` | Thread pool implementation, queue, per-task independent connections, progress reporting, cancel |
| `server/src/handler.c` | (No changes needed — existing GET/PUT handlers already support concurrent connections from different sockets) |
| `server/inc/protocol.h` | (No changes needed) |

---

## 12. Constraints & Edge Cases

- **Max selected files:** 128 (arbitrary, memory-safe)
- **Max queue size:** 256 tasks
- **Thread pool size:** 3 workers
- **Partial failure:** If 1 of 5 files fails, the other 4 still complete. Failed file reported in status bar briefly.
- **Double-click protection:** `g_transferring` flag OR queue non-empty prevents duplicate submission
- **Disconnect during transfer:** Cancel all transfers, clean up thread pool, close sockets, switch to login screen
- **Server restart mid-transfer:** Worker detects socket error, marks task as failed, continues to next task
