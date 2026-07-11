# Repeat File Detection (UI List Based) Spec

## Why
当前重复检测基于磁盘文件（`stat("./copy/...")` 和 `stat("./client/load/...")`），与用户在界面上看到的文件列表不一致：用户看到的是 LVGL 列表控件里展示的条目，而非磁盘真实状态。用户要求只要"展示出来的文件页面"里已有同名文件，就阻止上传/下载，并把弹窗文案统一改为 "repeat file"。

## What Changes
- **新增**：在 `ui_manager.c` 中维护两份"当前展示的文件名"镜像数组
  - `g_remote_displayed_files[]` —— 远程（server）列表当前展示的文件名
  - `g_local_displayed_files[]` —— 本地（client）列表当前展示的文件名
  - 在 `ui_update_file_list_cb` / `ui_update_local_file_list_cb` 填充列表控件时同步填充这两个数组
- **新增**：两个公共查询函数（声明在 `ui_manager.h`）
  - `bool ui_remote_list_has_file(const char *name)` —— 远程展示列表是否已有该文件名
  - `bool ui_local_list_has_file(const char *name)` —— 本地展示列表是否已有该文件名
- **修改**：`network_task.c` 中重复检测改为查询 UI 展示列表
  - `network_cmd_put()` 和 `network_cmd_put_multi()`：用 `ui_remote_list_has_file(base)` 替换 `check_file_exists("./copy", base)`
  - `start_download()`：用 `ui_local_list_has_file(filename)` 替换 `check_file_exists("./client/load", filename)`
- **修改**：弹窗文案从 "file have exist" 改为 "repeat file"（上传单文件、上传多文件、下载三处）
- **清理**：删除 `network_task.c` 中不再使用的 `check_file_exists`（当前存在两处重复定义，一并删除）

## Impact
- Affected specs:
  - `add-duplicate-file-detection` —— 本 spec 是对该功能的细化（检测源从磁盘改为 UI 列表，文案改为 "repeat file"）
- Affected code:
  - `client/ui_manager.c` —— 新增镜像数组、在两个列表回调中同步填充、新增两个查询函数
  - `client/ui_manager.h` —— 声明两个查询函数
  - `client/network_task.c` —— 替换检测逻辑、改文案、删除 `check_file_exists`
- 不修改：
  - `server/` —— 服务端不变
  - 传输队列 / 线程池逻辑不变

## ADDED Requirements

### Requirement: UI Displayed File List Tracking
系统 SHALL 在 UI 层维护"当前展示的文件名"列表，与 LVGL 列表控件内容保持同步。

#### Scenario: Remote list refresh
- **WHEN** 网络线程收到 LS 响应并调用 `ui_update_file_list_cb`
- **THEN** `g_remote_displayed_files[]` 被清空并重新填充为当前远程列表展示的所有文件名（不含 "." 、 ".." 、目录标记等非文件条目）

#### Scenario: Local list refresh
- **WHEN** 本地目录刷新调用 `ui_update_local_file_list_cb`
- **THEN** `g_local_displayed_files[]` 被清空并重新填充为当前本地列表展示的所有文件名（不含路径头文本、 ".." 、目录条目）

### Requirement: UI List Query API
系统 SHALL 提供两个公共函数供网络层查询展示列表中是否已有同名文件。

#### Scenario: Query remote list
- **WHEN** 调用 `ui_remote_list_has_file("test2.txt")`
- **AND** 远程展示列表中存在 "test2.txt"
- **THEN** 返回 true

#### Scenario: Query local list
- **WHEN** 调用 `ui_local_list_has_file("test2.txt")`
- **AND** 本地展示列表中存在 "test2.txt"
- **THEN** 返回 true

## MODIFIED Requirements

### Requirement: Upload Duplicate Detection
`network_cmd_put()` 和 `network_cmd_put_multi()` SHALL 在发送 PUT 指令前，提取 basename 并调用 `ui_remote_list_has_file(base)` 检查远程展示列表是否已有同名文件。如果存在，弹出 "repeat file" 弹窗并中止本次上传（多文件场景用 `continue` 跳过该文件，继续处理队列中其它文件）。

#### Scenario: Upload when remote list already shows the file
- **WHEN** 用户上传 `test2.txt`（或 `./client/test_delete/test2.txt`）
- **AND** 远程展示列表中已存在 "test2.txt"
- **THEN** 客户端弹出弹窗显示 "repeat file"
- **AND** 不发送 PUT 指令
- **AND** 用户点击 Close 关闭弹窗后可正常进行其它操作

#### Scenario: Upload when remote list does not show the file
- **WHEN** 用户上传 `test2.txt`
- **AND** 远程展示列表中不存在 "test2.txt"
- **THEN** 正常执行上传

### Requirement: Download Duplicate Detection
`start_download()` SHALL 在 `open()` 创建本地文件前，调用 `ui_local_list_has_file(filename)` 检查本地展示列表是否已有同名文件。如果存在，弹出 "repeat file" 弹窗、设置 `g_state = ST_IDLE` 并返回。

#### Scenario: Download when local list already shows the file
- **WHEN** 用户下载 `test2.txt`
- **AND** 本地展示列表中已存在 "test2.txt"
- **THEN** 客户端弹出弹窗显示 "repeat file"
- **AND** 不创建/覆盖本地文件
- **AND** 用户点击 Close 关闭弹窗后可正常进行其它操作

#### Scenario: Download when local list does not show the file
- **WHEN** 用户下载 `test2.txt`
- **AND** 本地展示列表中不存在 "test2.txt"
- **THEN** 正常执行下载

## REMOVED Requirements

### Requirement: Disk-based check_file_exists
**Reason**: 重复检测改为基于 UI 展示列表，不再需要 `stat()` 磁盘检测。
**Migration**: `network_task.c` 中 `check_file_exists`（两处重复定义）删除，所有调用点替换为 `ui_remote_list_has_file` / `ui_local_list_has_file`。

## 设计说明
1. **检测源**：上传检测查远程展示列表（传输目标侧），下载检测查本地展示列表（传输目标侧）。即"目标页面已展示同名文件则阻止"。
2. **线程安全**：镜像数组仅在 UI 线程的列表回调中写入，网络线程/worker 线程只读。采用简单直接访问（不加锁），与现有代码中 `g_selected_remote` 等全局变量的访问模式一致。最坏情况为读到稍过时的列表，对重复检测场景可接受。
3. **basename 提取**：上传时仍需从本地完整路径提取 basename（如 `./client/test_delete/test2.txt` → `test2.txt`）再与远程展示列表对比，保持与已有逻辑一致。
4. **弹窗复用**：继续复用 `ui_show_error_popup`（已有 Close 按钮和单例保护），仅改文案为 "repeat file"。
