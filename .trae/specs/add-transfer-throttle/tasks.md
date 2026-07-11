# Tasks

修改集中在 `client/network_task.c` 一个文件中。

- [ ] Task 1: 添加传输节流延迟宏和 include
  - [ ] 1.1 在 `#define CHUNK_SIZE 4096`（第 69 行）之后添加：
    ```
    #define CHUNK_DELAY_US 8000   /* 8ms delay per chunk for visible progress */
    ```
  - [ ] 1.2 确认 `#include <unistd.h>` 已存在（`usleep` 需要，文件顶部应已包含）

- [ ] Task 2: 在 `handle_download_chunk()` 中添加延迟
  - [ ] 2.1 在 `g_dl_received += w;` 之后、`update_dl_progress();` 之前（或之后）添加：
    ```
    usleep(CHUNK_DELAY_US);
    ```
    位置：第 682-683 行之间

- [ ] Task 3: 在 `handle_upload_chunk()` 中添加延迟
  - [ ] 3.1 在 `g_ul_sent += w;` 之后、`update_ul_progress();` 之前（或之后）添加：
    ```
    usleep(CHUNK_DELAY_US);
    ```
    位置：第 705-706 行之间

- [ ] Task 4: 降低下载进度更新阈值
  - [ ] 4.1 在 `update_dl_progress()`（第 520 行）将：
    ```
    if (pct - g_last_progress < 2 && pct < 100) return;
    ```
    改为：
    ```
    if (pct - g_last_progress < 1 && pct < 100) return;
    ```

- [ ] Task 5: 降低上传进度更新阈值
  - [ ] 5.1 在 `update_ul_progress()`（第 613 行）将：
    ```
    if (pct - g_last_progress < 2 && pct < 100) return;
    ```
    改为：
    ```
    if (pct - g_last_progress < 1 && pct < 100) return;
    ```

- [ ] Task 6: 验证 Close/Cancel 按钮行为（无需改代码，仅确认）
  - [ ] 6.1 确认 `on_close_progress_btn_clicked`（ui_manager.c 第 981 行）：仅销毁弹窗，不调用 `network_cancel_transfer()`
  - [ ] 6.2 确认 `on_cancel_btn_clicked`（ui_manager.c 第 999 行）：调用 `network_cancel_transfer()` + `ui_hide_progress()` + 显示 "Transfer cancelled"

- [ ] Task 7: 编译验证
  - [ ] 7.1 代码审查：确认所有修改正确
  - [ ] 7.2 用户在 Linux 虚拟机中编译验证

# Task Dependencies
- Task 1 是 Task 2 和 Task 3 的前提（需要宏定义）
- Task 4、Task 5 互相独立
- Task 6 独立（仅验证已有代码）
- Task 7 依赖 Task 1-6 完成

# 关键设计决策
1. **延迟值 8000 微秒（8ms）**：CHUNK_SIZE=4096 字节，8ms 延迟 → 约 512KB/s。对于小文件（几 KB），能产生约 1-2 秒的可见传输过程；对于大文件（几 MB），传输仍可在合理时间内完成。用户可根据需要调整 `CHUNK_DELAY_US`。
2. **阈值降到 1%**：原来 2% 阈值对于 50KB 以下的文件会跳过大部分更新（CHUNK_SIZE=4096，10 个 chunk 就传完，2% 阈值可能只触发 1-2 次更新）。降到 1% 后，每 1% 变化就更新，进度条更平滑。
3. **延迟放在 chunk 处理后**：不影响网络读取/写入的正确性，仅在每次 chunk 完成后暂停，给 UI 线程留出处理 `lv_async_call` 回调的时间。
4. **Close/Cancel 已正确实现**：Close 销毁弹窗不影响传输，Cancel 取消传输+隐藏弹窗。无需修改。
