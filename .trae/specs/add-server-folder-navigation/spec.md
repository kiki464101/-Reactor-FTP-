# Server Folder Navigation Spec

## Why
当前远程（server）列表中的文件夹无法通过单击导航进入，用户只能看到 `./copy/` 根目录下的文件。用户需要像单击本地文件夹一样，单击 server 端的文件夹即可进入该文件夹，展示 ".."（返回上级目录）以及里面的所有文件。这样用户可以在 server 端浏览子目录结构，查看上传的文件夹内容。

## What Changes

### 服务端
- **LS 命令支持路径参数**：`worker_handle_ls` 改为接收 payload 中的可选路径参数，列出 `MY_FTP_BOOT/path` 而非固定根目录。无 payload（plen < 12）时列出根目录，保持向后兼容。
- **路径遍历防护**：服务端 LS 和 GET 命令 SHALL 校验路径参数不含 ".." 且不以 "/" 开头（非绝对路径），防止目录遍历攻击。检测到非法路径时返回失败。
- **`worker_func` 调用签名修改**：`worker_handle_ls(sess)` 改为 `worker_handle_ls(sess, payload, plen)`。
- **`handler.h` 声明修改**：函数签名增加 payload 参数。

### 客户端 UI
- **远程当前路径跟踪**：新增 `g_remote_cur_path` 全局变量，镜像本地 `g_local_cur_path` 的设计。
- **LS 命令带路径**：`network_cmd_ls` 改为 `network_cmd_ls(const char *path)`，构建带路径参数的命令包。
- **远程单击导航**：`on_file_item_clicked` 改为：
  - 添加 `g_long_pressed` 抑制检查（长按后抑制单击导航，与本地一致）
  - ".." → 导航到父目录（截断 `g_remote_cur_path` 最后一段）
  - 以 "/" 结尾 → 导航进入子目录（追加到 `g_remote_cur_path`）
  - 普通文件 → 切换选中（存储完整相对路径 `g_remote_cur_path/filename`）
- **远程长按 g_long_pressed 修复**：`on_remote_file_item_long_pressed` 选中时 SHALL 设置 `g_long_pressed = true`，抑制后续 CLICKED 事件触发导航（与本地长按行为一致）。
- **远程列表 ".." 条目**：`ui_update_file_list_cb` 在 `g_remote_cur_path` 非空时，在列表顶部添加 ".." 条目和当前路径标题。
- **远程选中存储完整路径**：选中远程文件时，`g_selected_remote` 存储 `g_remote_cur_path/filename`（与本地 `g_selected_local` 设计一致），确保 GET 命令发送完整路径。
- **长按选中存储完整路径**：`on_remote_file_item_long_pressed` 选中时也存储完整相对路径。
- **刷新按钮重置远程路径**：`on_refresh_btn_clicked` 重置 `g_remote_cur_path` 为空（回到根目录）。
- **远程导航后重新请求 LS**：单击文件夹或 ".." 后，调用 `network_cmd_ls(g_remote_cur_path)` 重新获取列表。

### 子目录路径兼容性
- **下载流程验证**：子目录中选中文件路径含 "/"（如 "testfolder/file.txt"），需验证：
  - `start_download` 的 `mkdir_p` 正确创建本地子目录 `./client/load/testfolder/`
  - `network_cmd_get_multi` 的重复检测 `ui_local_list_has_entry(basename)` 正确提取 basename
  - 下载弹窗文件名显示友好（可仅显示 basename 或完整路径）
- **LISTDIR 路径兼容**：长按选中子目录中的文件夹下载时，LISTDIR 命令需能处理含前缀的路径（如 "subdir/testfolder/"）。

### 线程安全
- `g_remote_cur_path` 仅 UI 线程读写，与 `g_local_cur_path` 一致（UI 线程独占）
- LS 命令构建和发送在 UI 线程中完成（`network_cmd_ls` 已是 UI 线程调用）
- LS 响应通过 `lv_async_call(ui_update_file_list_cb, ...)` 从网络线程异步到 UI 线程
- `g_long_pressed` 为 UI 线程独占标志（仅 UI 事件回调中读写）

## Impact
- Affected specs:
  - `add-long-press-folder-transfer` — 远程长按选中需存储完整路径并设置 g_long_pressed，远程单击改为导航
  - `refine-repeat-file-detection` — 远程镜像数组在子目录时只含当前目录条目
- Affected code:
  - `server/inc/handler.h` — `worker_handle_ls` 签名修改
  - `server/src/handler.c` — `worker_handle_ls` 实现路径参数 + 路径遍历防护、`worker_func` 调用签名修改、`worker_handle_get` 增加路径遍历防护
  - `client/ui_manager.c` — `g_remote_cur_path`、`on_file_item_clicked` 导航逻辑 + g_long_pressed 抑制、`on_remote_file_item_long_pressed` 设置 g_long_pressed + 存储完整路径、`ui_update_file_list_cb` 添加 ".."、`on_refresh_btn_clicked` 重置远程路径
  - `client/network_task.h` — `network_cmd_ls` 签名修改
  - `client/network_task.c` — `network_cmd_ls` 实现路径参数

## ADDED Requirements

### Requirement: Path Traversal Protection
服务端 SHALL 对所有接受路径参数的命令（LS、GET、LISTDIR）进行路径遍历防护，拒绝包含 ".." 或以 "/" 开头的路径。

#### Scenario: LS with path traversal attempt
- **WHEN** 客户端发送 LS 命令且路径参数为 "../"
- **THEN** 服务端拒绝请求，返回失败 (result=0)
- **AND** 不列出任何目录内容

#### Scenario: GET with path traversal attempt
- **WHEN** 客户端发送 GET 命令且文件名为 "../etc/passwd"
- **THEN** 服务端拒绝请求，返回失败 (result=0)
- **AND** 不发送任何文件内容

#### Scenario: LISTDIR with path traversal attempt
- **WHEN** 客户端发送 LISTDIR 命令且路径参数为 "../secret"
- **THEN** 服务端拒绝请求，返回失败 (result=0)

#### Scenario: Normal subdirectory access
- **WHEN** 客户端发送 LS 命令且路径参数为 "testfolder/sub"
- **THEN** 服务端正常列出 `./copy/testfolder/sub/` 目录内容

### Requirement: Server LS With Path Parameter
服务端 LS 命令 SHALL 支持可选的路径参数，列出指定子目录下的文件和目录。无路径参数时列出根目录 `MY_FTP_BOOT`。

#### Scenario: LS root directory
- **WHEN** 客户端发送 LS 命令且无路径参数（payload 长度 < 12）
- **THEN** 服务端列出 `./copy/` 根目录下所有文件和目录
- **AND** 目录条目带 "/" 后缀

#### Scenario: LS subdirectory
- **WHEN** 客户端发送 LS 命令且路径参数为 "testfolder"
- **AND** `./copy/testfolder/` 存在
- **THEN** 服务端列出 `./copy/testfolder/` 下所有文件和目录
- **AND** 目录条目带 "/" 后缀

#### Scenario: LS non-existent subdirectory
- **WHEN** 客户端发送 LS 命令且路径参数为 "nonexist"
- **AND** `./copy/nonexist/` 不存在
- **THEN** 服务端响应失败 (result=0)

### Requirement: Remote Folder Navigation
客户端 SHALL 支持单击远程列表中的文件夹来导航进入该文件夹，展示其中的文件和 ".." 返回上级目录条目。长按后的 CLICKED 事件 SHALL 被抑制，避免长按选中后误触发导航。

#### Scenario: Click remote folder to navigate
- **WHEN** 用户单击远程列表中的文件夹条目（以 "/" 结尾）
- **AND** 前一个事件不是长按（`g_long_pressed` 为 false）
- **THEN** 更新 `g_remote_cur_path` 追加该文件夹名
- **AND** 发送 LS 命令获取该文件夹内容
- **AND** 列表刷新显示子目录内容
- **AND** 不选中该文件夹

#### Scenario: Long-press then click suppression
- **WHEN** 用户长按远程文件夹条目（触发 `on_remote_file_item_long_pressed`）
- **AND** `g_long_pressed` 被设置为 true
- **AND** 随后触发 CLICKED 事件
- **THEN** `on_file_item_clicked` 检测到 `g_long_pressed` 为 true
- **AND** 重置 `g_long_pressed` 为 false
- **AND** 不执行导航或选中操作

#### Scenario: Click ".." to go back
- **WHEN** 用户单击远程列表中的 ".." 条目
- **AND** `g_remote_cur_path` 非空
- **THEN** 截断 `g_remote_cur_path` 最后一段
- **AND** 发送 LS 命令获取父目录内容
- **AND** 列表刷新显示父目录内容

#### Scenario: Click remote file to select
- **WHEN** 用户单击远程列表中的文件条目（不以 "/" 结尾）
- **THEN** 切换该文件的选中状态
- **AND** 选中时存储完整相对路径（`g_remote_cur_path/filename`）到 `g_selected_remote`

#### Scenario: Long-press remote folder to select
- **WHEN** 用户长按远程列表中的文件夹条目
- **THEN** 选中该文件夹（不导航）
- **AND** 设置 `g_long_pressed = true` 抑制后续 CLICKED 事件
- **AND** 存储完整相对路径（`g_remote_cur_path/foldername/`）到 `g_selected_remote`

### Requirement: Remote ".." Entry Display
远程列表 SHALL 在非根目录时显示 ".." 条目和当前路径标题。

#### Scenario: Remote list at root
- **WHEN** `g_remote_cur_path` 为空（根目录）
- **THEN** 列表不显示 ".." 条目
- **AND** 不显示路径标题

#### Scenario: Remote list in subdirectory
- **WHEN** `g_remote_cur_path` 为 "testfolder"
- **THEN** 列表顶部显示当前路径标题（如 "Path: testfolder"）
- **AND** 列表显示 ".." 条目用于返回上级

### Requirement: Remote Selected File Full Path
远程选中文件时 SHALL 存储完整相对路径（含当前目录前缀），确保下载时 GET 命令发送正确路径。

#### Scenario: Select file in subdirectory
- **WHEN** `g_remote_cur_path` 为 "testfolder"
- **AND** 用户选中文件 "file.txt"
- **THEN** `g_selected_remote` 存储 "testfolder/file.txt"
- **AND** 下载时 GET 命令发送 "testfolder/file.txt"
- **AND** `start_download` 用 `mkdir_p` 创建本地子目录 `./client/load/testfolder/`
- **AND** 文件写入 `./client/load/testfolder/file.txt`

#### Scenario: Download file from subdirectory
- **WHEN** 用户在子目录 "testfolder" 中选中 "file.txt" 并下载
- **THEN** GET 命令发送 "testfolder/file.txt"
- **AND** 服务端 `worker_handle_get` 构建 `./copy/testfolder/file.txt` 路径
- **AND** 路径遍历检查通过（不含 ".."）
- **AND** 文件正确下载到本地 `./client/load/testfolder/file.txt`

## MODIFIED Requirements

### Requirement: network_cmd_ls
`network_cmd_ls` SHALL 接受可选路径参数，构建带路径的 LS 命令包。路径为 NULL 或空字符串时列出根目录。

### Requirement: worker_handle_ls
`worker_handle_ls` SHALL 接收 `(sess, payload, plen)` 参数。当 payload 包含路径时，先进行路径遍历检查（拒绝 ".." 和绝对路径），然后列出 `MY_FTP_BOOT/path` 目录；无路径时列出 `MY_FTP_BOOT` 根目录。目录条目添加 "/" 后缀（已有逻辑保持不变）。

### Requirement: worker_handle_get
`worker_handle_get` SHALL 在构建文件路径前进行路径遍历检查，拒绝包含 ".." 的文件名。

### Requirement: worker_handle_listdir
`worker_handle_listdir` SHALL 在构建目录路径前进行路径遍历检查，拒绝包含 ".." 的目录名。

### Requirement: worker_func TASK_LS case
`worker_func` 中 `TASK_LS` case SHALL 调用 `worker_handle_ls(sess, task.payload, task.payload_len)` 传递 payload。

### Requirement: on_file_item_clicked
`on_file_item_clicked`（远程单击）SHALL：
- 首先检查 `g_long_pressed` 标志，若为 true 则重置为 false 并返回（不导航不选中）
- ".." → 导航到父目录
- 以 "/" 结尾 → 导航进入子目录
- 普通文件 → 切换选中，存储完整相对路径

### Requirement: on_remote_file_item_long_pressed
`on_remote_file_item_long_pressed` SHALL：
- 选中时设置 `g_long_pressed = true`（抑制后续 CLICKED 事件）
- 存储完整相对路径（`g_remote_cur_path/foldername/` 或 `g_remote_cur_path/filename`）
- 取消选中时匹配完整相对路径

### Requirement: ui_update_file_list_cb
`ui_update_file_list_cb` SHALL 在 `g_remote_cur_path` 非空时：
- 在列表顶部添加当前路径标题
- 添加 ".." 条目（注册 CLICKED 和 LONG_PRESSED 回调）

### Requirement: on_refresh_btn_clicked
`on_refresh_btn_clicked` SHALL 重置 `g_remote_cur_path` 为空字符串，并调用 `network_cmd_ls(NULL)` 获取根目录列表。

## REMOVED Requirements
无

## 设计说明
1. **路径参数协议格式**：LS 命令的 payload 格式为 `[4B cmd_no][4B path_len][path]`，与 GET/LISTDIR 命令格式一致。无路径时 payload 仅含 `[4B cmd_no]`（8 字节），服务端检测 `plen < 12` 时列出根目录。
2. **远程路径与本地路径设计对称**：`g_remote_cur_path` 的设计完全镜像 `g_local_cur_path`，导航逻辑（".." 截断、"/" 追加）也对称。
3. **GET 命令需增加路径遍历检查**：服务端 `worker_handle_get` 用 `snprintf(path, "%s/%s", MY_FTP_BOOT, filename)` 构建路径。如果 filename 是 "testfolder/file.txt"，path 变成 "./copy/testfolder/file.txt"——这没问题。但如果 filename 含 ".."（如 "../etc/passwd"），会导致路径遍历。因此需在 GET/LS/LISTDIR 中增加路径遍历防护。
4. **PUT 命令路径遍历**：`worker_handle_put` 也用 filename 构建路径，但 filename 来自客户端上传，同样需要防护。不过 PUT 的 filename 是客户端自己决定的文件名，风险较低（用户上传到自己的目录），本 spec 暂不修改 PUT（保持最小改动）。
5. **路径遍历检查实现**：新增静态辅助函数 `is_path_safe(const char *path)`，检查路径不含 ".." 且不以 "/" 开头。在 LS、GET、LISTDIR 处理函数开头调用。
6. **g_long_pressed 修复**：`on_remote_file_item_long_pressed` 选中时设置 `g_long_pressed = true`，`on_file_item_clicked` 开头检查该标志——与本地 `on_local_file_item_clicked` 的抑制逻辑完全对称。
7. **子目录下载兼容性**：`start_download` 已有 `mkdir_p` 逻辑（之前 spec 实现），当 filename 含 "/" 时创建本地子目录。需验证路径拼接正确（`./client/load/` + "testfolder/file.txt"）。
8. **向后兼容**：LS 无路径参数时列出根目录，旧客户端（不发路径）仍能正常工作。
9. **线程安全**：`g_remote_cur_path` 仅 UI 线程访问，`g_long_pressed` 为 UI 线程独占标志，LS 命令在 UI 线程构建发送，LS 响应通过 `lv_async_call` 异步回调 UI 线程更新列表。
