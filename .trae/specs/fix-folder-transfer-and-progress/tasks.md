# Tasks

- [ ] Task 1: 新增刷新根目录接口
  - [ ] SubTask 1.1: `client/ui_manager.c` 新增 `ui_refresh_local_files_root()`：
    - `g_local_cur_path[0] = '\0';`
    - 调用 `ui_refresh_local_files();`
  - [ ] SubTask 1.2: `client/ui_manager.c` 新增 `ui_refresh_remote_list_root()`：
    - `g_remote_cur_path[0] = '\0';`
    - 调用 `network_cmd_ls(NULL);`
  - [ ] SubTask 1.3: `client/ui_manager.h` 声明两个新接口

- [ ] Task 2: 修改刷新回调使用新接口
  - [ ] SubTask 2.1: `client/network_task.c` 修改 `cb_refresh_local_list`：
    - 改为调用 `ui_refresh_local_files_root()` 而非 `ui_refresh_local_files()`
  - [ ] SubTask 2.2: `client/network_task.c` 修改 `cb_refresh_remote_list`：
    - 改为调用 `ui_refresh_remote_list_root()` 而非 `network_cmd_ls(NULL)`

- [ ] Task 3: 防重入检查
  - [ ] SubTask 3.1: `client/network_task.c` 修改 `network_cmd_get_multi` 入口：
    - `if (g_batch_active && g_tx_queue.count > 0)` → 弹窗 "transfer in progress"，返回 false
  - [ ] SubTask 3.2: `client/network_task.c` 修改 `network_cmd_put_multi` 入口：
    - 同 Task 3.1 逻辑

- [ ] Task 4: 验证 + 编译
  - [ ] SubTask 4.1: 下载 server 端文件夹 → 完成后本地列表显示文件夹条目
  - [ ] SubTask 4.2: 点击本地文件夹 → 进入查看文件
  - [ ] SubTask 4.3: 上传本地文件夹 → 完成后远程列表显示文件夹条目
  - [ ] SubTask 4.4: 点击远程文件夹 → 进入查看文件
  - [ ] SubTask 4.5: 传输过程中再次点击下载/上传 → 弹窗 "transfer in progress"
  - [ ] SubTask 4.6: 客户端编译通过无错误（用户在 Linux 虚拟机验证）

# Task Dependencies
- Task 1 独立
- Task 2 依赖 Task 1（使用新接口）
- Task 3 独立
- Task 4 依赖所有前置任务

# 并行执行建议
- Task 1 + Task 3 可并行
- Task 2 依赖 Task 1
