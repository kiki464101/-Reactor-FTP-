# Tasks

- [ ] Task 1: transfer_task_t新增local_path字段
  - [ ] SubTask 1.1: `client/network_task.h` 第40-43行，`transfer_task_t` 结构体新增 `char local_path[520]` 字段，注释"本地完整读取路径"

- [ ] Task 2: 修改network_cmd_put函数签名和实现
  - [ ] SubTask 2.1: `client/network_task.h` 第149行，`network_cmd_put` 签名改为 `bool network_cmd_put(const char *filename, const char *local_path)`
  - [ ] SubTask 2.2: `client/network_task.c` 第1565行，`network_cmd_put` 函数实现：
    - 用 `local_path` 替代 `normalize_local_path(filename, path, ...)`，直接 `stat(local_path, &st)` 和 `open(local_path, ...)`
    - `filename` 仅用于发送PUT命令（`build_cmd_put(filename, filesize, &len)`）和重复检查（basename提取）
    - `g_ul_filename` 存储服务器端文件名 `filename`（用于UI显示）
  - [ ] SubTask 2.3: 检查 `network_cmd_put` 的所有调用点，确保传递正确的参数

- [ ] Task 3: 修改start_upload使用local_path
  - [ ] SubTask 3.1: `client/network_task.c` 第897行，`start_upload` 函数签名改为 `start_upload(const char *filename, const char *local_path)`
  - [ ] SubTask 3.2: 用 `local_path` 替代 `normalize_local_path(filename, ul_path, ...)`，直接 `open(local_path, O_RDONLY)`
  - [ ] SubTask 3.3: `g_ul_filename` 存储 `filename`（服务器端文件名，用于UI显示和进度回调）
  - [ ] SubTask 3.4: 检查 `start_upload` 的调用点（第1050行 `ST_WAIT_PUT_RESP` 处理），传递 `local_path`

- [ ] Task 4: 修改batch_start_next传递local_path
  - [ ] SubTask 4.1: `client/network_task.c` 第693行，`batch_start_next` 中 `network_cmd_put(task.filename)` 改为 `network_cmd_put(task.filename, task.local_path)`
  - [ ] SubTask 4.2: 确保 `start_upload` 调用也传递 `local_path`（需在 `ST_WAIT_PUT_RESP` 状态保存 `local_path`）

- [ ] Task 5: 修改network_cmd_put_multi入队逻辑
  - [ ] SubTask 5.1: `client/network_task.c` 第1698-1731行，文件夹上传分支：
    - `task.filename` = `snprintf("%s/%s", sub_prefix, tasks[j].filename)` → "testupload/file1.txt"（保持不变）
    - `task.local_path` = `snprintf("%s/%s", local_base, tasks[j].filename)` → "./client/load/testupload/file1.txt"（新增）
  - [ ] SubTask 5.2: `client/network_task.c` 第1734-1761行，普通文件上传分支：
    - `task.filename` = basename（从 `filenames[i]` 提取，如 "hello.txt"）
    - `task.local_path` = `normalize_local_path(filenames[i], path, ...)` → "./client/load/hello.txt"（新增）
  - [ ] SubTask 5.3: 验证入队后 `task.filename` 不含父目录前缀（如 "load/"），`task.local_path` 包含完整本地路径

- [ ] Task 6: 保存local_path供start_upload使用
  - [ ] SubTask 6.1: `client/network_task.c` 新增全局变量 `static char g_ul_local_path[520]`，在 `network_cmd_put` 中存储 `local_path`
  - [ ] SubTask 6.2: `ST_WAIT_PUT_RESP` 状态处理中（第1030行附近），调用 `start_upload(g_ul_filename, g_ul_local_path)`
  - [ ] SubTask 6.3: `finish_upload` 中重置 `g_ul_local_path[0] = '\0'`

- [ ] Task 7: 验证和测试
  - [ ] SubTask 7.1: 编译客户端无错误
  - [ ] SubTask 7.2: 测试在根目录上传普通文件，服务器端文件名正确
  - [ ] SubTask 7.3: 测试在子目录（如load/）上传普通文件，服务器端只显示basename，不含 "load/" 前缀
  - [ ] SubTask 7.4: 测试在子目录（如load/）上传文件夹（如testupload/），服务器端只创建 testupload/ 目录，不含 load/ 目录
  - [ ] SubTask 7.5: 测试文件夹下所有文件内容完整传输
  - [ ] SubTask 7.6: 验证本地文件能正确读取（stat/open成功）

# Task Dependencies
- Task 1 独立（结构体修改）
- Task 2 依赖 Task 1（需要local_path字段）
- Task 3 依赖 Task 2（start_upload被network_cmd_put调用）
- Task 4 依赖 Task 2（batch_start_next调用network_cmd_put）
- Task 5 依赖 Task 1（入队填充local_path）
- Task 6 依赖 Task 2 和 Task 3（全局变量连接两个函数）
- Task 7 依赖所有前置任务

# 并行执行建议
- Task 1 优先执行
- Task 5 可与 Task 2 并行（都依赖Task 1，但修改不同函数）
