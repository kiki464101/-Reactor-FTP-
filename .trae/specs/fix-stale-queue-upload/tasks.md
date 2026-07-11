# Tasks

修改集中在 `client/network_task.c` 一个文件中。

- [ ] Task 1: 修复 `tx_queue_reset()` 清空队列
  - [ ] 1.1 在 `tx_queue_reset()`（第 313-318 行）中，在 `q->cancelled = false;` 之后添加三行：
    ```
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    ```
  - 修改后完整函数：
    ```
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

- [ ] Task 2: 编译验证
  - [ ] 2.1 代码审查：确认修改正确
  - [ ] 2.2 用户在 Linux 虚拟机中编译验证

# Task Dependencies
- Task 2 依赖 Task 1 完成

# 关键设计决策
1. **根因**：`tx_queue_reset()` 被 `network_cmd_put_multi()` 和 `network_cmd_get_multi()` 在入队前调用，原意是"重置队列以备新批次使用"。但原实现只重置了 `cancelled` 标志，队列中上一批次的旧任务（`head`/`tail`/`count` 指向的位置）仍然保留。工作线程会从队列中取出旧任务并执行，导致用户看到"整个文件夹都被传过去"。
2. **修复方式**：在 `tx_queue_reset()` 中同时重置 `head`/`tail`/`count` 为 0，逻辑上等价于清空队列。这与 `tx_queue_cancel_all()` 的清空逻辑一致（第 309 行 `q->head = q->tail = q->count = 0`）。
3. **线程安全**：修改在 `pthread_mutex_lock` 保护下进行，与原有代码一致。
4. **仅改一行函数**：最小改动，不影响其它逻辑。`network_cmd_put_multi` 中的 `tx_queue_reset` 调用不变，`network_cmd_get_multi` 同理受益。
