# Fix Stale Queue Tasks Upload Bug Spec

## Why
`tx_queue_reset()` 只重置了 `cancelled` 标志，没有清空队列中的旧任务（`head`/`tail`/`count` 未重置）。当用户在子目录中选中文件上传时，之前批次遗留的旧任务会和新任务一起被处理，导致"整个文件夹都被传过去"的现象。

## What Changes
- 修复 `tx_queue_reset()`，在重置 `cancelled` 标志的同时清空队列（重置 `head`/`tail`/`count`）

## Impact
- Affected code:
  - `client/network_task.c` — `tx_queue_reset()` 函数（第 313-318 行）
- 不需要修改的文件：
  - `client/ui_manager.c` — 选中逻辑正确
  - `server/` — 服务端不涉及

## ADDED Requirements
无

## MODIFIED Requirements

### Requirement: tx_queue_reset
`tx_queue_reset()` SHALL 在重置 `cancelled` 标志的同时，清空队列中的所有任务。

**Previous**:
```c
static void tx_queue_reset(transfer_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    q->cancelled = false;
    pthread_mutex_unlock(&q->mutex);
}
```

**New**:
```c
static void tx_queue_reset(transfer_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    q->cancelled = false;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_unlock(&q->mutex);
}
```

## REMOVED Requirements
无
