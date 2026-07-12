# Add Folder Recursive Delete And Fix Download Path Spec

## Why
当前 `execute_local_delete` 跳过目录条目（"skip directory entries"），无法删除选中的文件夹。同时 `start_download` 对文件夹下载路径不加 `load/` 前缀（下载到 `./client/<文件夹名>/`），与用户期望的 `./client/load/<文件夹名>/` 不一致。用户需要：长按选中文件夹后可删除（递归删除整个文件夹内容）、下载（到 `./client/load/<文件夹名>/`）、上传（到 `./copy/<文件夹名>/`），重复时弹窗 "Dirent has exist"，传输时进度条弹窗可 close/cancel。**下载/上传的文件夹进入时必须显示 ".." 条目以返回上级目录，不能忽略。**

## What Changes

### 修复1：文件夹递归删除
- **`execute_local_delete` 支持目录条目**：不再跳过以 "/" 结尾的条目，改为递归删除整个文件夹内容（子文件夹、子文件、文件夹本身）。
- **新增 `remove_recursive(const char *path)` 辅助函数**：
  - `stat(path)` 检查类型
  - 若是目录 → `opendir` + `readdir` 遍历，对每个子条目递归调用 `remove_recursive(path/child)`，最后 `rmdir(path)`
  - 若是文件 → `unlink(path)`
  - 跳过 "." 和 ".."
- **删除 server 文件夹**：保持现有 "error delete" 弹窗（已有逻辑，`on_delete_btn_clicked` 检查远程选中）。

### 修复2：文件夹下载路径加 `load/` 前缀
- **`start_download` 路径统一**：所有下载（文件和文件夹）都到 `./client/load/<filename>`。
  - 文件夹下载（filename 含 "/"，如 `myfolder/sub/file.txt`）→ `./client/load/myfolder/sub/file.txt`
  - 普通文件下载 → `./client/load/<filename>`（保持现有约定）
  - 用 `mkdir_p` 创建必要的子目录（已有逻辑）
- **移除 `is_folder_dl` 分支**：删除 `strchr(filename, '/')` 判断，统一用 `./client/load/%s` 路径。

### 修复3：确保文件夹进入时有 ".." 返回上级
- **本地列表 ".."**（已有逻辑，验证即可）：
  - `scan_local_directory` 在 `g_local_cur_path` 非空时添加 ".." 条目（第 1197-1199 行）
  - `on_local_file_item_clicked` 点击 ".." 截断 `g_local_cur_path` 最后一段并刷新（第 1469-1473 行）
  - 下载文件夹到 `./client/load/<文件夹名>/` 后，用户进入 `load/` → `<文件夹名>/`，每层都有 ".." 返回上级
- **远程列表 ".."**（UI 已有，依赖 server 端 LS 支持子目录）：
  - `ui_update_file_list_cb` 在 `g_remote_cur_path` 非空时添加 ".." 按钮（第 1116-1121 行）
  - `on_file_item_clicked` 点击 ".." 截断 `g_remote_cur_path` 并调用 `network_cmd_ls`（第 723-728 行）
  - **依赖**：server 端 `worker_handle_ls` 当前不支持子目录路径参数（`add-server-folder-navigation` spec 未完成）。上传文件夹到 `./copy/<文件夹名>/` 后，进入该文件夹需要 server 端 LS 返回子目录内容。本 spec 不实现 server 端子目录 LS，但明确标注此依赖。

### 验证：已有功能（无需修改）
- **长按选中文件夹**：`on_local_file_item_long_pressed`/`on_remote_file_item_long_pressed` 已实现，"." 和 ".." 忽略。
- **上传文件夹**：`network_cmd_put_multi` 检测目录 → `collect_files_recursive` 递归遍历 → 逐个入队 PUT。
- **上传重复检测**：`ui_remote_list_has_entry(basename)` 检查，弹窗 "Dirent has exist"。
- **下载文件夹**：`network_cmd_get` 检测尾部 "/" → 发送 LISTDIR → 入队多个 GET 任务。
- **下载重复检测**：`ui_local_list_has_entry(basename)` 检查，弹窗 "Dirent has exist"。
- **进度条弹窗**：`ui_show_progress_batch` 已有，Close 隐藏（传输继续），Cancel 调用 `network_cancel_transfer`。
- **服务器端创建文件夹**：`worker_handle_put` 用 `mkdir_p` 递归创建目录（已有逻辑）。

## Impact
- Affected specs:
  - `add-long-press-folder-transfer` — `start_download` 路径改回加 `load/` 前缀
  - `add-delete-feature` — `execute_local_delete` 支持递归删除文件夹
  - `add-server-folder-navigation` — 远程文件夹进入时 ".." 依赖此 spec 完成 server 端子目录 LS
- Affected code:
  - `client/ui_manager.c` — `execute_local_delete` 支持目录、新增 `remove_recursive`
  - `client/network_task.c` — `start_download` 路径统一加 `load/` 前缀

## ADDED Requirements

### Requirement: Folder Recursive Delete
用户长按选中客户端文件夹后点击 Delete，SHALL 递归删除整个文件夹内容（子文件夹、子文件、文件夹本身）。

#### Scenario: Delete selected folder
- **WHEN** 用户长按选中客户端文件夹 `myfolder/`
- **AND** 点击 Delete 按钮
- **AND** 确认 Yes
- **THEN** `remove_recursive("./client/myfolder")` 递归删除
- **AND** 所有子文件夹和子文件被删除
- **AND** `myfolder/` 文件夹本身被删除
- **AND** 本地列表刷新后不再显示 `myfolder/`

#### Scenario: Delete folder with subfolders
- **WHEN** 选中文件夹 `myfolder/` 包含 `sub/` 子文件夹和 `file.txt` 文件
- **AND** 点击 Delete → Yes
- **THEN** `remove_recursive` 先删除 `sub/` 内的文件，再 `rmdir(sub/)`
- **AND** 删除 `myfolder/` 内的文件
- **AND** 最后 `rmdir(myfolder/)`

#### Scenario: Delete server folder
- **WHEN** 选中 server 端文件夹
- **AND** 点击 Delete
- **THEN** 弹窗 "error delete"（已有逻辑）
- **AND** 不执行删除

### Requirement: Folder Download Path
文件夹下载时，文件 SHALL 下载到 `./client/load/<文件夹名>/<文件路径>`。

#### Scenario: Download folder to load/
- **WHEN** 用户长按选中 server 端文件夹 `myfolder/` 并下载
- **THEN** LISTDIR 返回 `myfolder/sub/file.txt` 等路径
- **AND** 文件下载到 `./client/load/myfolder/sub/file.txt`
- **AND** `mkdir_p` 创建 `./client/load/myfolder/sub/` 子目录
- **AND** 用户进入 `load/` → `myfolder/` 可看到下载的文件
- **AND** 每层目录都有 ".." 返回上级

### Requirement: Dotdot Entry In Folders
下载/上传的文件夹进入时 SHALL 显示 ".." 条目，点击可返回上级目录，不能忽略。

#### Scenario: Enter downloaded folder
- **WHEN** 文件夹下载到 `./client/load/myfolder/` 完成
- **AND** 用户进入 `load/` → 点击 `myfolder/`
- **THEN** 本地列表显示 `myfolder/` 内的文件 + ".." 条目
- **AND** 点击 ".." 返回 `load/` 目录

#### Scenario: Enter uploaded folder (依赖 server 子目录 LS)
- **WHEN** 文件夹上传到 `./copy/myfolder/` 完成
- **AND** 用户在远程列表点击 `myfolder/`
- **THEN** 远程列表显示 `myfolder/` 内的文件 + ".." 条目
- **AND** 点击 ".." 返回 `./copy/` 根目录
- **NOTE** 此场景依赖 `add-server-folder-navigation` spec 完成 server 端 `worker_handle_ls` 支持子目录路径参数

## MODIFIED Requirements

### Requirement: execute_local_delete
`execute_local_delete` SHALL 不再跳过目录条目，改为：
- 遍历 `g_selected_local[]`
- 对每个条目构建路径 `./client/<name>`（name 可能含子目录前缀和尾部 "/"）
- 调用 `remove_recursive(path)` 递归删除
- 统计成功/失败计数
- 刷新本地列表

### Requirement: remove_recursive
新增 `remove_recursive(const char *path)` 函数：
- `stat(path)` 获取类型
- 若是目录（`S_ISDIR`）→ `opendir` + `readdir`，跳过 "." 和 ".."，对每个子条目构建 `path/child` 递归调用 `remove_recursive`，最后 `rmdir(path)`
- 若是文件（`S_ISREG`）→ `unlink(path)`
- 返回 0 成功，-1 失败

### Requirement: start_download
`start_download` SHALL 统一使用 `./client/load/<filename>` 路径，不再区分文件夹/普通文件：
- 移除 `is_folder_dl` 分支和 `strchr(filename, '/')` 判断
- 路径构建：`snprintf(dl_path, sizeof(dl_path), "./client/load/%s", filename)`
- `mkdir_p` 创建必要的子目录（已有逻辑，路径变量调整）

## REMOVED Requirements
无

## 核心风险点（三句话总结）
1. **server 端子目录 LS 未实现**：`worker_handle_ls` 当前不接受路径参数，上传文件夹后进入该文件夹会显示 `./copy/` 根目录内容而非子目录内容，".." 虽有但内容错误——此问题需 `add-server-folder-navigation` spec 解决，本 spec 不处理。
2. **递归删除边界情况**：`remove_recursive` 需正确处理权限不足、非空目录残留、符号链接等边界，否则可能删除失败或留下残余文件，影响后续创建同名文件夹。
3. **下载路径层级深**：文件夹下载到 `./client/load/<文件夹名>/<子路径>/`，用户需进入 `load/` 再进入文件夹才能看到内容，路径层级较深，且重复检测基于 UI 显示列表（镜像数组），若用户在子目录时刷新可能漏检根目录已有同名文件夹。
