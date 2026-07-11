# Long-Press Folder Transfer Spec

## Why
当前本地列表中点击文件夹会直接打开（导航进入），无法选中文件夹进行整体上传；远程列表中的目录条目无法被识别和整体下载。用户需要长按选中文件夹（不打开），选中后可上传/下载整个文件夹（递归传输所有文件，保留目录结构）。"." 和 ".." 无法被选中。上传/下载重复文件夹时弹窗 "Dirent has exist"，点击 Close 关闭弹窗不影响后续操作。传输时弹出进度条弹窗，点击 Close 关闭弹窗不影响传输，点击 Cancel 取消传输。

## 架构与线程安全说明

### 服务端架构（Reactor + Thread Pool — Half-sync/Half-reactive）
服务端已采用 **epoll + 线程池** 架构，本 spec 新增的 LISTDIR 命令完全复用现有模式：

1. **主线程（Reactor）**：`epoll_wait` 边沿触发 → `accept` 新连接 / `handle_client_read` 读取命令包
2. **命令分发**（`dispatch_command`）：解析 cmd_no → 构造 `task_t` → `epoll_ctl(EPOLL_CTL_DEL)` 从 epoll 摘除 fd → `thread_pool_submit` 提交任务
3. **线程池**（8 个 worker 线程）：`pthread_mutex_lock` + `pthread_cond_wait` 从任务队列取任务 → 独占访问该客户端 fd 执行命令 → 完成后 `epoll_ctl(EPOLL_CTL_MOD)` 重新挂回 epoll
4. **LISTDIR 复用此模式**：客户端发送 LISTDIR → 主线程分发到 `TASK_LISTDIR` → worker 线程执行 `worker_handle_listdir`（递归遍历 + 发送响应）→ 完成后 rearm fd

**线程安全保证**：
- 任务队列 `thread_pool_t` 内置 `pthread_mutex_t mutex` + `pthread_cond_t cond`，多 worker 并发取任务安全
- 每个 fd 在任一时刻只被一个 worker 独占访问（epoll 摘除期间），无数据竞争
- 共享内存 `g_shm` 通过 `shm_update_status` 更新客户端状态（已有同步机制）

### 客户端架构（单网络线程 + 线程安全队列）
客户端采用 **单网络线程 + poll() + 线程安全传输队列** 模式：

1. **UI 线程**：处理 LVGL 事件（长按、单击、按钮点击），调用 `network_cmd_*` API
2. **网络线程**（`network_thread_func`）：`poll()` 监听 socket → 状态机驱动传输 → `lv_async_call` 回调 UI
3. **传输队列**（`g_tx_queue`）：`pthread_mutex_t mutex` + `pthread_cond_t cond` 保护，UI 线程 push 任务、网络线程 pop 任务，线程安全
4. **批量传输**：`batch_start_next()` 从队列取任务 → 调用 `network_cmd_get/put` → 状态机驱动单文件传输 → 完成后取下一个

**文件夹传输的完整逻辑链条**：

**上传流程**：
```
UI 长按选中本地文件夹 "myfolder/"
  → 用户点击 Upload 按钮
  → on_upload_btn_clicked 收集 g_selected_local[]
  → network_cmd_put_multi(filenames, count)
    → 遍历选中项，检测 "/" 结尾
    → 是目录:
       → folder_basename("myfolder/") → "myfolder"
       → ui_remote_list_has_entry("myfolder") 检测重复
       → 重复 → lv_async_call 弹窗 "Dirent has exist"，continue 跳过
       → 不重复 → collect_files_recursive("./client/myfolder/", "")
         → opendir + readdir 递归遍历
         → 跳过 "." 和 ".."
         → 子目录 → 递归，rel_prefix 更新为 "subname/"
         → 文件 → 构建 task(filename="myfolder/file.txt", is_upload=true)
       → 逐个 tx_queue_push 到 g_tx_queue（mutex 保护）
    → 是文件: 保持现有逻辑（stat + basename + 重复检测 + push）
  → 网络线程 ST_IDLE 状态检测到 queue.count > 0
  → batch_start_next → network_cmd_put
  → 发送 PUT 命令 → ST_WAIT_PUT_RESP
  → 服务端 epoll 收到 → dispatch_command → TASK_PUT → thread_pool_submit
  → worker 线程 worker_handle_put → mkdir_p 创建子目录 → 接收文件内容
  → 完成后 rearm fd → 发送 FTP_CMD_DONE
  → 客户端收到 DONE → finish_upload → 取下一个任务
  → 循环直到队列空 → batch_check_complete → "Transfer complete"
```

**下载流程**：
```
UI 长按选中远程文件夹 "myfolder/"
  → 用户点击 Download 按钮
  → on_download_btn_clicked 收集 g_selected_remote[]
  → network_cmd_get_multi(filenames, count)
    → 遍历选中项，检测 "/" 结尾
    → 是目录:
       → folder_basename("myfolder/") → "myfolder"
       → ui_local_list_has_entry("myfolder") 检测重复
       → 重复 → lv_async_call 弹窗 "Dirent has exist"，continue 跳过
       → 不重复 → 入队 task(filename="myfolder/", is_upload=false)
    → 是文件: 保持现有逻辑
  → 网络线程 batch_start_next → network_cmd_get("myfolder/")
  → 检测尾部 "/" → 调用 network_cmd_listdir("myfolder/")
  → 发送 FTP_CMD_LISTDIR 命令 → ST_WAIT_LISTDIR_RESP
  → 服务端 epoll 收到 → dispatch_command → TASK_LISTDIR → thread_pool_submit
  → worker 线程 worker_handle_listdir → 递归遍历 ./copy/myfolder/
  → 响应 "myfolder/file1.txt\nmyfolder/sub/file2.txt\n"
  → 客户端网络线程 ST_WAIT_LISTDIR_RESP 收到响应
  → 解析文件列表 → 为每个文件 tx_queue_push 到 g_tx_queue
  → g_state = ST_IDLE
  → batch_start_next → network_cmd_get("myfolder/file1.txt")
  → 发送 GET → ST_WAIT_GET_RESP → start_download
  → start_download 检测 filename 含 "/" → mkdir_p 创建 ./client/load/myfolder/
  → 接收文件内容写入 ./client/load/myfolder/file1.txt
  → 完成后取下一个 → 直到队列空
```

**传输速率保证**：
- 服务端 epoll 边沿触发 + 线程池 8 worker 并发处理多客户端
- 客户端传输队列支持批量入队，网络线程持续处理无需 UI 干预
- 单文件传输使用 4KB chunk + `usleep(100ms)` 仅用于进度条可见性，可调整
- 文件夹传输 = 多文件批量传输，复用现有 `ui_show_progress_batch` 面板

## What Changes

### 服务端
- **LS 标记目录**：`worker_handle_ls` 给目录条目添加 "/" 后缀，使客户端能区分文件和目录
- **新命令 LISTDIR (1033)**：递归列出指定目录下所有文件路径（相对于 `./copy/`）
  - 新增 `FTP_CMD_LISTDIR = 1033` 到 `protocol.h`
  - 新增 `TASK_LISTDIR = 5` 到 `thread_pool.h`
  - `main.c` `dispatch_command` 新增 LISTDIR case：`epoll_ctl(DEL)` + `thread_pool_submit(TASK_LISTDIR)`（复用现有模式）
  - `handler.c` 新增 `worker_handle_listdir` 实现：opendir + readdir 递归遍历，线程安全（worker 独占 fd）
  - `worker_func` switch 新增 `TASK_LISTDIR` case
  - `handler.h` 声明 `worker_handle_listdir`

### 客户端 UI
- **长按事件**：本地和远程列表均添加 `LV_EVENT_LONG_PRESSED` 处理
  - 本地列表：长按文件夹→选中（不导航），长按文件→选中，长按 ".."→忽略
  - 远程列表：长按任意条目→选中（与单击效果一致），"." 和 ".."→忽略
  - 单击行为不变（本地文件夹单击仍导航，远程单击仍选中）
- **本地镜像数组**：`ui_update_local_file_list_cb` 改为也存储目录条目（含 "/" 后缀），支持文件夹重复检测
- **新查询函数**：`ui_remote_list_has_entry(name)` / `ui_local_list_has_entry(name)` —— 检查 "name" 和 "name/" 两种形式

### 客户端上传
- `network_cmd_put_multi` 检测目录条目（以 "/" 结尾），递归遍历本地文件夹，为每个文件入队 PUT 任务（保留相对路径如 "myfolder/sub/file.txt"）
- 服务端已有 `mkdir_p` 自动创建子目录，无需修改 PUT 处理
- 重复文件夹检测：提取 basename，检查远程列表是否已有同名条目 → "Dirent has exist"
- 线程安全：`tx_queue_push` 内部 `pthread_mutex_lock` 保护，UI 线程 push 与网络线程 pop 无竞争

### 客户端下载
- `network_cmd_get_multi` 检测目录条目，入队带 "/" 后缀的 GET 任务
- `network_cmd_get` 检测尾部 "/"，发送 LISTDIR 命令代替 GET，进入 `ST_WAIT_LISTDIR_RESP` 状态
- 网络线程收到 LISTDIR 响应后解析文件列表，为每个文件入队 GET 任务（`tx_queue_push` 线程安全）
- `start_download` 创建本地子目录（mkdir -p）当文件名含 "/"
- 重复文件夹检测：提取 basename，检查本地列表是否已有同名条目 → "Dirent has exist"

### 进度条
- 复用现有 `ui_show_progress_batch` 批量进度面板
- Close 按钮：隐藏弹窗，传输继续（已有行为）
- Cancel 按钮：`network_cancel_transfer` → `tx_queue_cancel_all`（`pthread_cond_broadcast` 唤醒所有等待 worker）+ 设置 `g_transfer_cancelled`

## Impact
- Affected specs:
  - `refine-repeat-file-detection` — 本地镜像数组需改为也存储目录条目
  - `add-duplicate-file-detection` — 重复检测扩展到文件夹场景
- Affected code:
  - `server/inc/protocol.h` — 新增 `FTP_CMD_LISTDIR = 1033`
  - `server/inc/thread_pool.h` — 新增 `TASK_LISTDIR = 5`
  - `server/src/main.c` — `dispatch_command` 新增 LISTDIR 分发（复用 epoll DEL + thread_pool_submit 模式）
  - `server/src/handler.c` — LS 标记目录、新增 `worker_handle_listdir`、`worker_func` 新增 case
  - `server/inc/handler.h` — 声明 `worker_handle_listdir`
  - `client/ui_manager.c` — 长按事件、镜像数组改存目录、新增查询函数
  - `client/ui_manager.h` — 声明新查询函数
  - `client/network_task.c` — 文件夹上传遍历、下载 LISTDIR 流程、mkdir -p、重复检测、新状态 `ST_WAIT_LISTDIR_RESP`

## ADDED Requirements

### Requirement: Long-Press Folder Selection
系统 SHALL 支持长按列表条目来选中文件夹（不打开/不导航）。

#### Scenario: Long-press local folder
- **WHEN** 用户长按本地列表中的文件夹条目（以 "/" 结尾）
- **THEN** 该文件夹被选中（高亮 + 加入 `g_selected_local`）
- **AND** 不导航进入该文件夹
- **AND** 单击该文件夹仍可正常导航进入

#### Scenario: Long-press local file
- **WHEN** 用户长按本地列表中的文件条目
- **THEN** 该文件被选中（与单击选中效果一致）

#### Scenario: Long-press ".." entry
- **WHEN** 用户长按本地列表中的 ".." 条目
- **THEN** 不做任何操作（不选中、不导航）

#### Scenario: Long-press remote item
- **WHEN** 用户长按远程列表中的任意条目
- **THEN** 该条目被选中（与单击选中效果一致）
- **AND** "." 和 ".." 无法被选中

### Requirement: Server LS Directory Marking
服务端 LS 响应 SHALL 给目录条目添加 "/" 后缀，使客户端能区分文件和目录。

#### Scenario: LS with directories
- **WHEN** `./copy/` 下有文件 "a.txt" 和目录 "subdir"
- **THEN** LS 响应包含 "a.txt\n" 和 "subdir/\n"

### Requirement: Server LISTDIR Command
服务端 SHALL 支持新命令 LISTDIR (1033)，递归列出指定目录下所有文件的相对路径。LISTDIR 通过现有 epoll + 线程池架构处理：主线程 epoll 收到命令 → 从 epoll 摘除 fd → 提交 `TASK_LISTDIR` 到线程池 → worker 线程独占 fd 执行递归遍历 → 发送响应 → rearm fd。

#### Scenario: LISTDIR a folder
- **WHEN** 客户端发送 LISTDIR "myfolder"
- **AND** `./copy/myfolder/` 下有 "file1.txt" 和子目录 "sub/" 含 "file2.txt"
- **THEN** 服务端响应包含 "myfolder/file1.txt\nmyfolder/sub/file2.txt\n"

#### Scenario: LISTDIR non-existent folder
- **WHEN** 客户端发送 LISTDIR "nonexist"
- **AND** `./copy/nonexist/` 不存在
- **THEN** 服务端响应失败 (result=0)

### Requirement: Folder Upload
客户端 SHALL 在上传时检测选中的目录条目，递归遍历并上传所有文件，保留目录结构。上传流程通过线程安全传输队列管理：UI 线程 `tx_queue_push`（mutex 保护）→ 网络线程 `batch_start_next` → `network_cmd_put` → 服务端线程池 `worker_handle_put`（`mkdir_p` 自动创建子目录）。

#### Scenario: Upload a folder
- **WHEN** 用户选中本地文件夹 "myfolder/" 并点击 Upload
- **AND** 远程列表无同名条目
- **THEN** 客户端递归遍历 `./client/myfolder/` 下所有文件
- **AND** 为每个文件入队 PUT 任务（路径如 "myfolder/file.txt"）
- **AND** 服务端通过 mkdir_p 自动创建子目录

#### Scenario: Upload duplicate folder
- **WHEN** 用户选中本地文件夹 "myfolder/" 并点击 Upload
- **AND** 远程列表已存在 "myfolder" 或 "myfolder/"
- **THEN** 弹窗显示 "Dirent has exist"
- **AND** 点击 Close 关闭弹窗后可正常进行其它操作
- **AND** 跳过该文件夹，继续处理其它选中项

### Requirement: Folder Download
客户端 SHALL 在下载时检测选中的目录条目，通过 LISTDIR 获取文件列表后逐个下载，保留目录结构。下载流程：UI 线程入队带 "/" 后缀的 GET 任务 → 网络线程 `batch_start_next` → `network_cmd_get` 检测 "/" → 发送 LISTDIR → `ST_WAIT_LISTDIR_RESP` → 解析响应入队多个 GET 任务 → 逐个下载（`start_download` 用 `mkdir_p` 创建本地子目录）。

#### Scenario: Download a folder
- **WHEN** 用户选中远程文件夹 "myfolder/" 并点击 Download
- **AND** 本地列表无同名条目
- **THEN** 客户端发送 LISTDIR 获取文件列表
- **AND** 为每个文件入队 GET 任务
- **AND** `start_download` 创建本地子目录（mkdir -p）后写入文件

#### Scenario: Download duplicate folder
- **WHEN** 用户选中远程文件夹 "myfolder/" 并点击 Download
- **AND** 本地列表已存在 "myfolder" 或 "myfolder/"
- **THEN** 弹窗显示 "Dirent has exist"
- **AND** 点击 Close 关闭弹窗后可正常进行其它操作
- **AND** 跳过该文件夹，继续处理其它选中项

### Requirement: Folder Transfer Progress Bar
文件夹传输时 SHALL 显示进度条弹窗，与文件传输进度条类似。进度条通过 `lv_async_call` 从网络线程异步更新到 UI 线程，确保线程安全。

#### Scenario: Progress bar during folder transfer
- **WHEN** 文件夹传输开始
- **THEN** 弹出进度条弹窗显示传输进度
- **AND** 进度条随传输进度更新

#### Scenario: Close button
- **WHEN** 用户点击 Close 按钮
- **THEN** 弹窗关闭
- **AND** 传输继续进行不受影响
- **AND** 其它功能可正常运行

#### Scenario: Cancel button
- **WHEN** 用户点击 Cancel 按钮
- **THEN** 传输被取消（`tx_queue_cancel_all` 清空队列 + `pthread_cond_broadcast` 唤醒等待的 worker）
- **AND** 弹窗关闭

## MODIFIED Requirements

### Requirement: Local Mirror Array
`ui_update_local_file_list_cb` SHALL 在填充 `g_local_displayed_files[]` 时同时存储目录条目（以 "/" 后缀），不再跳过目录。文件条目保持原样存储。镜像数组仅由 UI 线程写入，网络线程只读，单写者模式保证线程安全。

### Requirement: network_cmd_put_multi
`network_cmd_put_multi` SHALL 检测以 "/" 结尾的条目（目录），递归遍历本地文件夹并为每个文件入队 PUT 任务。检测到重复文件夹时弹窗 "Dirent has exist" 并跳过。所有队列操作通过 `tx_queue_push`（内部 `pthread_mutex_lock`）保证线程安全。

### Requirement: network_cmd_get_multi
`network_cmd_get_multi` SHALL 检测以 "/" 结尾的条目（目录），入队带 "/" 后缀的 GET 任务。检测到重复文件夹时弹窗 "Dirent has exist" 并跳过。

### Requirement: network_cmd_get
`network_cmd_get` SHALL 检测文件名尾部 "/"，若为目录则发送 LISTDIR 命令并进入 `ST_WAIT_LISTDIR_RESP` 状态，代替普通 GET 流程。

### Requirement: start_download
`start_download` SHALL 在文件名含 "/" 时先创建本地子目录（递归 mkdir），再创建文件。

### Requirement: worker_handle_ls
`worker_handle_ls` SHALL 对每个条目用 stat 检查是否为目录，若是则添加 "/" 后缀。

### Requirement: dispatch_command
`dispatch_command` SHALL 在 `main.c` 中新增 `FTP_CMD_LISTDIR` case，复用现有 `epoll_ctl(EPOLL_CTL_DEL)` + `thread_pool_submit(TASK_LISTDIR)` 模式。

### Requirement: worker_func
`worker_func` SHALL 在 switch 中新增 `TASK_LISTDIR` case 调用 `worker_handle_listdir`，完成后 rearm fd（复用现有 rearm 逻辑）。

## REMOVED Requirements
无

## 设计说明
1. **长按不替换单击**：本地列表单击文件夹仍导航（保持现有行为），长按是新增的选中方式。远程列表单击仍选中（保持现有行为），长按是等效的选中方式。
2. **文件夹路径表示**：选中文件夹时在 `g_selected_local` / `g_selected_remote` 中存储完整相对路径（含 "/" 后缀），如 "test_delete/myfolder/"。上传遍历时去掉尾部 "/" 构建本地路径，保留路径前缀构建 PUT 文件名。
3. **LISTDIR 复用现有连接和 epoll 模式**：LISTDIR 命令通过主 socket 发送，服务端复用 epoll + 线程池模式处理（与 GET/PUT 一致）。新增 `ST_WAIT_LISTDIR_RESP` 状态，收到响应后解析文件列表并入队 GET 任务。
4. **服务端 mkdir_p 已存在**：上传时服务端 `worker_handle_put` 已有 `mkdir_p` 递归创建目录逻辑，无需修改 PUT 处理。
5. **重复检测用 basename**：从文件夹路径提取 basename（如 "test_delete/myfolder/" → "myfolder"），检查目标列表是否已有 "name" 或 "name/" 条目。
6. **进度条复用**：文件夹传输本质是批量文件传输，复用现有 `ui_show_progress_batch` 面板。Close 仅隐藏弹窗（传输继续），Cancel 调用 `network_cancel_transfer`（`tx_queue_cancel_all` + `pthread_cond_broadcast` 取消传输）。
7. **递归遍历跳过 "." 和 ".."**：上传遍历本地文件夹时跳过 "." 和 ".." 条目，避免无限循环。
8. **线程安全总结**：
   - 服务端：epoll 摘除 fd 保证 worker 独占、线程池 mutex/cond 保护任务队列
   - 客户端：`g_tx_queue` 内置 mutex/cond、镜像数组单写者模式（UI 线程写、网络线程读）、`lv_async_call` 跨线程通信
   - 取消机制：`tx_queue_cancel_all` 设置 `cancelled` 标志 + `pthread_cond_broadcast` 唤醒所有阻塞在 `tx_queue_pop` 的 worker
