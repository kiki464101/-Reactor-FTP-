# Fix Folder Display After Transfer Spec

## Why
下载/上传文件夹后，自动刷新逻辑使用当前的 `g_local_cur_path`/`g_remote_cur_path` 扫描目录。如果用户在子目录中操作，刷新的是子目录而非根目录，导致新下载/上传的文件夹条目不可见。用户期望下载文件夹后 client 端列表显示该文件夹，上传文件夹后 server 端列表显示该文件夹，点击可进入查看内容。

## What Changes

### 修复1：刷新时重置路径到根目录
- **新增 `ui_refresh_local_files_root()`**：重置 `g_local_cur_path` 为空，然后调用 `ui_refresh_local_files()` 扫描 `./client/` 根目录。
- **新增 `ui_refresh_remote_list_root()`**：重置 `g_remote_cur_path` 为空，然后调用 `network_cmd_ls(NULL)` 获取根目录列表。
- **`cb_refresh_local_list`** 改为调用 `ui_refresh_local_files_root()`。
- **`cb_refresh_remote_list`** 改为调用 `ui_refresh_remote_list_root()`。

### 修复2：防重入（简化版）
- **`network_cmd_get_multi` 入口检查**：若 `g_batch_active` 为 true 且 `g_tx_queue.count > 0`，弹窗 "transfer in progress"，返回 false。
- **`network_cmd_put_multi` 入口检查**：同上。
- 防止用户重复点击下载/上传导致队列重置和 `g_state` 覆盖，中断正在进行的传输。

### 验证：文件夹显示和导航（已有逻辑，无需修改）
- **下载文件夹路径**：`start_download` 已修复——filename 含 "/" 时下载到 `./client/<filename>`（不加 `load/` 前缀）。
- **本地列表显示文件夹**：`scan_local_directory` 对目录条目添加 "/" 后缀，`ui_update_local_file_list_cb` 存储所有条目（含目录）。
- **点击文件夹导航**：`on_local_file_item_clicked` 对 "/" 结尾的条目追加到 `g_local_cur_path` 并刷新。
- **远程列表显示文件夹**：`worker_handle_ls` 对目录添加 "/" 后缀，`ui_update_file_list_cb` 存储所有条目。
- **点击远程文件夹导航**：`on_file_item_clicked` 对 "/" 结尾的条目追加到 `g_remote_cur_path` 并调用 `network_cmd_ls`。
- **".." 返回上级**：本地和远程列表在子目录时均显示 ".." 条目，点击返回上级目录。

## Impact
- Affected specs:
  - `add-long-press-folder-transfer` — 刷新逻辑改为重置路径到根目录
  - `add-server-folder-navigation` — 远程刷新重置 `g_remote_cur_path`
- Affected code:
  - `client/ui_manager.c` — 新增 `ui_refresh_local_files_root()` 和 `ui_refresh_remote_list_root()`
  - `client/ui_manager.h` — 声明新接口
  - `client/network_task.c` — `cb_refresh_local_list`/`cb_refresh_remote_list` 改调新接口、`network_cmd_get_multi`/`network_cmd_put_multi` 加防重入

## ADDED Requirements

### Requirement: Refresh Root After Transfer
批量传输完成后 SHALL 刷新根目录列表（重置当前路径），使新下载/上传的文件夹条目立即可见。

#### Scenario: Download folder while in subdirectory
- **WHEN** 用户在本地子目录 `load/` 中
- **AND** 下载 server 端文件夹 `hahahah/` 完成
- **THEN** `g_local_cur_path` 重置为空
- **AND** 本地列表刷新显示 `./client/` 根目录内容
- **AND** 根目录列表显示 `hahahah/` 文件夹条目
- **AND** 用户点击 `hahahah/` 可进入查看文件

#### Scenario: Upload folder while in remote subdirectory
- **WHEN** 用户在远程子目录中
- **AND** 上传本地文件夹完成
- **THEN** `g_remote_cur_path` 重置为空
- **AND** 远程列表刷新显示根目录内容
- **AND** 根目录列表显示新上传的文件夹条目

### Requirement: Transfer Reentrance Protection
当批量传输正在进行时，新的下载/上传请求 SHALL 被拒绝，弹窗提示 "transfer in progress"。

#### Scenario: Download while transfer in progress
- **WHEN** `g_batch_active` 为 true 且 `g_tx_queue.count > 0`
- **AND** 用户点击下载按钮
- **THEN** `network_cmd_get_multi` 返回 false
- **AND** 弹窗显示 "transfer in progress"
- **AND** 当前传输不受影响

## MODIFIED Requirements

### Requirement: cb_refresh_local_list
`cb_refresh_local_list` SHALL 调用 `ui_refresh_local_files_root()`（重置 `g_local_cur_path` 为空后刷新），而非 `ui_refresh_local_files()`。

### Requirement: cb_refresh_remote_list
`cb_refresh_remote_list` SHALL 调用 `ui_refresh_remote_list_root()`（重置 `g_remote_cur_path` 为空后调用 `network_cmd_ls(NULL)`），而非直接 `network_cmd_ls(NULL)`。

### Requirement: network_cmd_get_multi
`network_cmd_get_multi` SHALL 在入口检查 `g_batch_active && g_tx_queue.count > 0`，若为 true 则弹窗 "transfer in progress" 并返回 false。

### Requirement: network_cmd_put_multi
`network_cmd_put_multi` SHALL 在入口检查 `g_batch_active && g_tx_queue.count > 0`，若为 true 则弹窗 "transfer in progress" 并返回 false。

## REMOVED Requirements
无

## 设计说明
1. **根因分析**：`cb_refresh_local_list` 调用 `ui_refresh_local_files()`，后者用 `g_local_cur_path` 扫描目录。如果用户在 `load/` 子目录，扫描的是 `./client/load/` 而非 `./client/`，看不到根目录下新下载的文件夹。修复：刷新前重置 `g_local_cur_path` 为空。
2. **远程同理**：`cb_refresh_remote_list` 调用 `network_cmd_ls(NULL)` 获取根目录列表，但 `ui_update_file_list_cb` 根据 `g_remote_cur_path` 显示。如果 `g_remote_cur_path` 非空，会添加 ".." 和路径标题，但实际内容是根目录的——显示错乱。修复：刷新前重置 `g_remote_cur_path` 为空。
3. **防重入**：从 DEBUG 日志看，用户多次点击下载导致 `g_batch_total` 从 3 变 1，队列被重置。简单的 `g_batch_active && g_tx_queue.count > 0` 检查即可防止。
4. **文件夹导航已有**：`on_local_file_item_clicked` 和 `on_file_item_clicked` 已处理 "/" 结尾的文件夹导航，`scan_local_directory` 和 `worker_handle_ls` 已对目录添加 "/" 后缀。无需修改。
5. **".." 已有**：本地 `scan_local_directory` 在子目录时添加 ".."，远程 `ui_update_file_list_cb` 在 `g_remote_cur_path` 非空时添加 ".."。点击 ".." 返回上级目录的逻辑也已实现。无需修改。
6. **线程安全**：`cb_refresh_local_list`/`cb_refresh_remote_list` 通过 `lv_async_call` 在 UI 线程执行，重置 `g_local_cur_path`/`g_remote_cur_path`（UI 线程独占）安全。防重入检查在 UI 线程（`network_cmd_get_multi`/`put_multi` 由 UI 按钮调用）。
