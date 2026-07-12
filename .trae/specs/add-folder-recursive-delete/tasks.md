# Tasks

- [ ] Task 1: 新增递归删除辅助函数
  - [ ] SubTask 1.1: `client/ui_manager.c` 新增 `remove_recursive(const char *path)` 静态函数：
    - `stat(path)` 获取类型
    - 若 `S_ISDIR` → `opendir` + `readdir` 遍历，跳过 "." 和 ".."，对每个子条目构建 `path/child` 递归调用 `remove_recursive`，遍历完后 `closedir`，最后 `rmdir(path)`
    - 若 `S_ISREG` → `unlink(path)`
    - 返回 0 成功，-1 失败
  - [ ] SubTask 1.2: 添加 `#include <unistd.h>`（如未有，用于 `unlink`/`rmdir`）

- [ ] Task 2: 修改 `execute_local_delete` 支持文件夹
  - [ ] SubTask 2.1: `client/ui_manager.c` 修改 `execute_local_delete`：
    - 删除 "skip directory entries" 逻辑（第 953-956 行的 `if (nlen > 0 && name[nlen - 1] == '/') { fail++; continue; }`）
    - 对每个选中条目构建路径 `./client/<name>`
    - 调用 `remove_recursive(path)` 替代 `remove(path)`
    - 根据返回值统计成功/失败
  - [ ] SubTask 2.2: 验证文件夹条目（尾部 "/"）路径构建正确（`./client/myfolder/` 或 `./client/load/myfolder/`）
  - [ ] SubTask 2.3: 验证 `remove_recursive` 对尾部 "/" 路径的处理（内部 stat 不受尾部 "/" 影响）

- [ ] Task 3: 修改 `start_download` 路径统一加 `load/` 前缀
  - [ ] SubTask 3.1: `client/network_task.c` 修改 `start_download`（第 592-599 行）：
    - 删除 `bool is_folder_dl = (strchr(filename, '/') != NULL);` 判断
    - 删除 `if (is_folder_dl)` 分支
    - 统一使用 `snprintf(dl_path, sizeof(dl_path), "./client/load/%s", filename);`
  - [ ] SubTask 3.2: 验证 `mkdir_p` 仍能正确创建子目录（路径变为 `./client/load/myfolder/sub/`，`mkdir_p` 递归创建）
  - [ ] SubTask 3.3: 验证普通文件下载仍到 `./client/load/<filename>`

- [ ] Task 4: 验证 ".." 返回上级功能
  - [ ] SubTask 4.1: 下载文件夹到 `./client/load/<文件夹名>/` → 进入 `load/` → 有 ".." 返回根目录
  - [ ] SubTask 4.2: 进入 `<文件夹名>/` → 有 ".." 返回 `load/`
  - [ ] SubTask 4.3: 验证 `scan_local_directory` 在子目录时添加 ".."（第 1197-1199 行已有逻辑）
  - [ ] SubTask 4.4: 验证 `on_local_file_item_clicked` 点击 ".." 返回上级（第 1469-1473 行已有逻辑）
  - [ ] SubTask 4.5: 远程列表 ".." 依赖 `add-server-folder-navigation` spec（本 spec 不实现，标注依赖）

- [ ] Task 5: 验证 + 编译
  - [ ] SubTask 5.1: 长按选中客户端文件夹 → Delete → Yes → 文件夹及所有内容被删除
  - [ ] SubTask 5.2: 长按选中 server 文件夹 → Delete → 弹窗 "error delete"
  - [ ] SubTask 5.3: 长按选中 server 文件夹 → Download → 文件下载到 `./client/load/<文件夹名>/`
  - [ ] SubTask 5.4: 下载重复文件夹 → 弹窗 "Dirent has exist"
  - [ ] SubTask 5.5: 长按选中本地文件夹 → Upload → server 端创建 `./copy/<文件夹名>/`
  - [ ] SubTask 5.6: 上传重复文件夹 → 弹窗 "Dirent has exist"
  - [ ] SubTask 5.7: 上传/下载时进度条弹窗显示，close 隐藏弹窗传输继续，cancel 取消传输
  - [ ] SubTask 5.8: 客户端编译通过无错误（用户在 Linux 虚拟机验证）

# Task Dependencies
- Task 1 独立（新增辅助函数）
- Task 2 依赖 Task 1（使用 `remove_recursive`）
- Task 3 独立（修改 `start_download` 路径）
- Task 4 依赖 Task 3（验证下载路径后的 ".." 功能）
- Task 5 依赖所有前置任务

# 并行执行建议
- Task 1 + Task 3 可并行（修改不同文件，无冲突）
- Task 2 依赖 Task 1
- Task 4 依赖 Task 3

# 外部依赖
- **Task 4.5 远程列表 ".."**：依赖 `add-server-folder-navigation` spec 完成 server 端 `worker_handle_ls` 支持子目录路径参数。该 spec 当前所有 task 未勾选（未完成）。本 spec 不实现此功能，仅验证本地 ".." 和标注远程依赖。
