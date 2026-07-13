# 修复上传文件夹包含父目录路径 Spec

## Why
用户在 `client/load/` 目录下长按选中 `testupload/` 文件夹上传时，`task.filename` 只存储 "testupload/file1.txt"（服务器端文件名），但 `network_cmd_put` 用 `normalize_local_path` 重建本地路径时丢失了 "load/" 前缀，导致找不到本地文件。如果保留 "load/" 前缀，服务器端会创建 `load/testupload/` 结构。根本原因是 `transfer_task_t.filename` 同时承担"本地读取路径"和"服务器端文件名"两个冲突职责。

## What Changes
- **`transfer_task_t` 新增 `local_path` 字段**：存储本地完整读取路径
- **`network_cmd_put_multi` 入队时填充两个字段**：`filename` = 服务器端文件名（如 "testupload/file1.txt"），`local_path` = 本地完整路径（如 "./client/load/testupload/file1.txt"）
- **`network_cmd_put` 接受 `local_path` 参数**：用 `local_path` 读取本地文件，用 `filename` 发送给服务器
- **`start_upload` 使用 `local_path` 打开文件**

## Impact
- Affected specs: fix-subdirectory-upload, fix-upload-filename-flatten, add-long-press-folder-transfer
- Affected code:
  - `client/network_task.h`: `transfer_task_t` 新增 `local_path` 字段
  - `client/network_task.c`: `network_cmd_put_multi`、`network_cmd_put`、`start_upload`、`batch_start_next`

## 问题根本原因分析

### 当前流程（有bug）

1. 用户在 `client/load/` 目录下长按选中 `testupload/` 文件夹
2. `g_selected_local[0]` = "load/testupload/"（UI拼接当前路径+文件名）
3. `network_cmd_put_multi` 收到 "load/testupload/"
4. `folder_basename("load/testupload/", sub_prefix, ...)` → "testupload"
5. `collect_files_recursive("./client/load/testupload", "", ...)` 收集文件
6. 入队 `task.filename` = "testupload/file1.txt"（sub_prefix + 相对路径）
7. `batch_start_next` 调用 `network_cmd_put("testupload/file1.txt")`
8. `normalize_local_path("testupload/file1.txt", path)` → `./client/testupload/file1.txt`
9. **BUG**: 实际文件在 `./client/load/testupload/file1.txt`，`stat`/`open` 失败

### 冲突点

`task.filename` = "testupload/file1.txt" 这个值：
- 用于服务器端文件名：✓ 正确（服务器应创建 `./copy/testupload/file1.txt`）
- 用于本地读取路径：✗ 错误（`normalize_local_path` 生成 `./client/testupload/file1.txt`，丢失 "load/"）

如果改成 `task.filename` = "load/testupload/file1.txt"：
- 用于本地读取路径：✓ 正确（`./client/load/testupload/file1.txt`）
- 用于服务器端文件名：✗ 错误（服务器创建 `./copy/load/testupload/file1.txt`，多了 `load/`）

### 解决方案

分离职责：`filename` 用于服务器端，`local_path` 用于本地读取。

## ADDED Requirements

### Requirement: transfer_task_t新增local_path字段
`transfer_task_t` SHALL 新增 `char local_path[520]` 字段，存储本地完整读取路径。

#### Scenario: 文件夹上传任务入队
- **WHEN** 用户上传 `load/testupload/` 文件夹下的 `file1.txt`
- **THEN** `task.filename` = "testupload/file1.txt"（服务器端文件名）
- **AND** `task.local_path` = "./client/load/testupload/file1.txt"（本地读取路径）

### Requirement: network_cmd_put接受local_path参数
`network_cmd_put` SHALL 接受 `local_path` 参数用于本地文件读取，`filename` 参数用于发送给服务器。

#### Scenario: 上传子目录文件
- **WHEN** 上传 `load/testupload/` 下的 `file1.txt`
- **THEN** 用 `local_path` = "./client/load/testupload/file1.txt" 打开本地文件
- **AND** 用 `filename` = "testupload/file1.txt" 发送PUT命令给服务器
- **AND** 服务器在 `./copy/testupload/file1.txt` 创建文件（不含 `load/`）

## MODIFIED Requirements

### Requirement: network_cmd_put_multi入队逻辑
`network_cmd_put_multi` SHALL 在入队时同时填充 `filename`（服务器端文件名）和 `local_path`（本地完整路径）。

**文件夹上传**：
- `filename` = `sub_prefix + "/" + relative_path`（如 "testupload/file1.txt"）
- `local_path` = `local_base + "/" + relative_path`（如 "./client/load/testupload/file1.txt"）

**普通文件上传**：
- `filename` = basename（如 "hello.txt"）
- `local_path` = `normalize_local_path(filenames[i])`（如 "./client/load/hello.txt"）

### Requirement: batch_start_next传递local_path
`batch_start_next` SHALL 调用 `network_cmd_put(task.filename, task.local_path)` 传递两个参数。

### Requirement: start_upload使用local_path
`start_upload` SHALL 使用 `local_path` 参数打开本地文件，使用 `filename` 参数设置 `g_ul_filename`（用于UI显示和进度回调）。
