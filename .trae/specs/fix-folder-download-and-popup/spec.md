# Fix Folder Download And Popup Stacking Spec

## Why
下载文件夹时存在三个问题：(1) 文件夹下的部分文件（如 `siusiu.txt`）下载失败或被跳过，用户在 `./client/load/hahahah/` 下找不到；(2) 下载后需要显示到 `client/load/` 下，点开文件夹查看所有内容（当前路径已正确，但流程有 bug 导致文件缺失）；(3) 下载时进度条弹窗和重复检测错误弹窗同时出现（弹窗套用），用户每次只需要一个弹窗。

## What Changes

### 修复1：弹窗套用（核心问题）
- **`on_download_btn_clicked`/`on_upload_btn_clicked` 延迟创建进度条弹窗**：不在调用 `network_cmd_get_multi`/`network_cmd_put_multi` 之前就 `ui_show_progress_batch()`，改为在第一个文件实际开始传输时（`start_download`/`start_upload` 成功创建文件后）才通过 `lv_async_call` 创建进度条弹窗。
- **`ui_show_error_popup` 调用时隐藏进度条弹窗**：在 `ui_show_error_popup` 入口检查 `batch_prog_panel` 是否存在且可见，若是则隐藏（`LV_OBJ_FLAG_HIDDEN`），避免两个弹窗叠加。
- **重复检测统一在 `network_cmd_get_multi`/`network_cmd_put_multi` 入口**：`start_download`/`start_upload` 中不再做重复检测（删除 `ui_local_list_has_entry` 检查），避免传输过程中弹窗打断。重复检测只在入队前做一次。

### 修复2：文件夹下载文件缺失
- **`network_cmd_get_multi` 目录入队时立即检查重复**：对文件夹条目（尾部 "/"），提取 basename 用 `ui_local_list_has_entry` 检查，重复则弹窗 "Dirent has exist" 并 `continue` 跳过整个文件夹，不发送 LISTDIR。
- **`start_download` 删除重复检测**：移除 `ui_local_list_has_entry(filename)` 检查和 "repeat file" 弹窗（第 585-590 行），避免下载过程中因重复检测中断单个文件。重复检测已在入队前完成。
- **`ST_WAIT_LISTDIR_RESP` 入队时不检查重复**：LISTDIR 返回的文件路径直接入队，不在入队时检查重复（因为 `ui_local_list_has_entry` 检查的是 UI 显示名，不匹配含路径的 filename 如 `hahahah/siusiu.txt`）。
- **验证 LISTDIR 返回所有文件**：确认 `listdir_recursive` 递归遍历所有子目录，返回所有文件路径（含目录前缀）。

### 修复3：下载路径和显示（已有行为，验证）
- **下载路径**：`start_download` 统一用 `./client/load/<filename>`（第 594 行已有），文件夹下载到 `./client/load/hahahah/siusiu.txt`。
- **`mkdir_p` 创建子目录**：`start_download` 第 596-605 行已有逻辑，创建 `./client/load/hahahah/` 子目录。
- **本地列表显示文件夹**：下载完成后 `cb_refresh_local_list` 刷新本地列表，用户进入 `load/` → `hahahah/` 可看到下载的文件。

## Impact
- Affected specs:
  - `add-long-press-folder-transfer` — `start_download` 删除重复检测，弹窗创建时机调整
  - `fix-folder-transfer-and-progress` — 进度条弹窗创建时机改为延迟创建
- Affected code:
  - `client/ui_manager.c` — `on_download_btn_clicked`/`on_upload_btn_clicked` 移除 `ui_show_progress_batch` 同步调用、`ui_show_error_popup` 隐藏进度条弹窗
  - `client/network_task.c` — `start_download` 删除重复检测、新增 `cb_show_progress_batch` 异步回调、`batch_start_next` 在第一个任务开始时触发进度条弹窗

## ADDED Requirements

### Requirement: Single Popup At A Time
任何时刻 UI 上 SHALL 只有一个弹窗（进度条弹窗或错误弹窗），不允许弹窗叠加。

#### Scenario: Repeat folder detected during download
- **WHEN** 用户长按选中 server 端文件夹 `hahahah/` 并点击下载
- **AND** 本地列表已显示 `hahahah/` 文件夹
- **THEN** `network_cmd_get_multi` 入口检测到重复
- **AND** 弹窗显示 "Dirent has exist"（仅此一个弹窗，无进度条弹窗）
- **AND** 不发送 LISTDIR 请求
- **AND** 点击 close 关闭弹窗

#### Scenario: Progress popup during transfer
- **WHEN** 文件夹下载开始（LISTDIR 返回文件列表，第一个文件开始传输）
- **THEN** 进度条弹窗显示（仅此一个弹窗，无错误弹窗）
- **AND** 进度条随传输进度更新
- **AND** 点击 close 隐藏弹窗，传输继续
- **AND** 点击 cancel 取消整个文件夹传输

#### Scenario: Error popup hides progress popup
- **WHEN** 进度条弹窗可见
- **AND** 传输过程中出现错误（如 server 返回 "not found"）
- **THEN** `ui_show_error_popup` 隐藏进度条弹窗
- **AND** 显示错误弹窗（仅此一个）
- **AND** 点击 close 关闭错误弹窗

### Requirement: Folder Download Completes All Files
文件夹下载 SHALL 下载文件夹下所有文件（含子目录中的文件），不跳过、不中断。

#### Scenario: Download folder with multiple files
- **WHEN** 用户下载 server 端文件夹 `hahahah/`（包含 `siusiu.txt`、`henghengheng.txt`）
- **THEN** LISTDIR 返回 `hahahah/siusiu.txt` 和 `hahahah/henghengheng.txt`
- **AND** 两个文件都被入队为 GET 任务
- **AND** 两个文件都下载到 `./client/load/hahahah/`
- **AND** 用户进入 `load/` → `hahahah/` 可看到两个文件

#### Scenario: Download folder with subdirectory
- **WHEN** 文件夹 `hahahah/` 包含子目录 `sub/` 和 `sub/file.txt`
- **THEN** LISTDIR 返回 `hahahah/sub/file.txt`
- **AND** `mkdir_p` 创建 `./client/load/hahahah/sub/`
- **AND** 文件下载到 `./client/load/hahahah/sub/file.txt`

## MODIFIED Requirements

### Requirement: on_download_btn_clicked
`on_download_btn_clicked` SHALL 不在调用 `network_cmd_get_multi` 之前调用 `ui_show_progress_batch`：
- 移除第 813 行 `ui_show_progress_batch();`
- 进度条弹窗改为在第一个文件实际开始传输时由网络线程通过 `lv_async_call(cb_show_progress_batch, NULL)` 创建
- 如果 `network_cmd_get_multi` 返回 false（重复或无选中），不创建进度条弹窗

### Requirement: on_upload_btn_clicked
`on_upload_btn_clicked` SHALL 同理移除第 831 行 `ui_show_progress_batch();`，改为延迟创建。

### Requirement: ui_show_error_popup
`ui_show_error_popup` SHALL 在入口检查并隐藏进度条弹窗：
- 若 `batch_prog_panel` 存在且可见（`!lv_obj_has_flag(batch_prog_panel, LV_OBJ_FLAG_HIDDEN)`）→ `lv_obj_add_flag(batch_prog_panel, LV_OBJ_FLAG_HIDDEN)` 隐藏
- 然后创建/更新错误弹窗

### Requirement: start_download
`start_download` SHALL 移除重复检测逻辑：
- 删除第 585-590 行 `ui_local_list_has_entry(filename)` 检查和 "repeat file" 弹窗
- 重复检测已在 `network_cmd_get_multi` 入队前完成，传输过程中不再检查
- 直接构建路径 `./client/load/<filename>` 并 `mkdir_p` + `open`

### Requirement: network_cmd_get_multi
`network_cmd_get_multi` SHALL 对文件夹条目在入队前检查重复：
- 文件夹条目（尾部 "/"）→ 提取 basename → `ui_local_list_has_entry(basename)` 检查
- 重复 → `lv_async_call(cb_show_error_popup, make_str_data("Dirent has exist"))`，`continue` 跳过
- 不重复 → 调用 `network_cmd_listdir` 发送 LISTDIR 请求
- 普通文件条目 → 保持现有逻辑（basename + `ui_local_list_has_entry` 检查）

### Requirement: cb_show_progress_batch
新增 `cb_show_progress_batch(void *data)` 异步回调：
- 调用 `ui_show_progress_batch()`
- 由网络线程在第一个文件开始传输时通过 `lv_async_call` 触发

### Requirement: batch_start_next
`batch_start_next` SHALL 在第一个任务开始传输时触发进度条弹窗创建：
- 检查 `g_batch_done == 0`（第一个任务）
- 且任务成功开始（`start_download`/`start_upload` 设置 `g_state` 为传输状态）
- 调用 `lv_async_call(cb_show_progress_batch, NULL)`

## REMOVED Requirements
无

## 核心风险点（三句话总结）
1. **弹窗时序竞争**：进度条弹窗改为延迟创建后，如果第一个文件传输很快完成，弹窗可能在传输结束后才创建，导致用户看到弹窗闪现——需确保弹窗创建在传输开始后立即触发，且传输完成时不立即销毁弹窗。
2. **重复检测移除风险**：`start_download` 移除重复检测后，如果 `network_cmd_get_multi` 入队前漏检（如 LISTDIR 返回的文件路径含子目录前缀，`ui_local_list_has_entry` 不匹配），可能导致覆盖已下载的文件——需确认入队前检查覆盖所有场景。
3. **LISTDIR 文件缺失调试**：用户报告 `siusiu.txt` 找不到，需确认 server 端 `./copy/hahahah/siusiu.txt` 文件确实存在，且 `listdir_recursive` 返回的路径与 `worker_handle_get` 构建的路径一致——可能需要在 server 端加日志调试。
