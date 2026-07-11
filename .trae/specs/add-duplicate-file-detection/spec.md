# Duplicate File Detection Spec

## Why
上传时服务器 `./copy/` 下可能已存在同名文件，下载时客户端 `./client/load/` 下也可能已存在同名文件，当前代码直接用 `O_CREAT | O_TRUNC` 静默覆盖，用户无感知。需要在传输前检测重复文件，弹出弹窗提示 "file have exist"，用户点击 Close 关闭弹窗后可正常进行其它操作。

## What Changes
- 上传前：客户端检查服务器端是否已有同名文件（通过 LS 列表对比）
- 下载前：客户端检查本地 `./client/load/` 是否已有同名文件
- 检测到重复时：弹出 "file have exist" 弹窗，中止本次传输，不覆盖

## Impact
- Affected code:
  - `client/network_task.c` — `network_cmd_put()` 和 `start_download()` 增加重复检测
- 不需要修改的文件：
  - `client/ui_manager.c` — `ui_show_error_popup` 已有 Close 按钮，直接复用
  - `server/` — 不修改服务端

## ADDED Requirements

### Requirement: Upload Duplicate Detection
系统 SHALL 在上传文件前，检查服务器共享目录是否已存在同名文件。

#### Scenario: Upload Duplicate File
- **WHEN** 用户上传文件 `test2.txt`，且服务器 `./copy/test2.txt` 已存在
- **THEN** 客户端弹出弹窗显示 "file have exist"
- **AND** 中止本次上传，不发送 PUT 指令
- **AND** 用户点击 Close 关闭弹窗后可正常进行其它操作

#### Scenario: Upload New File
- **WHEN** 用户上传文件 `test2.txt`，且服务器 `./copy/` 下无此文件
- **THEN** 正常执行上传

### Requirement: Download Duplicate Detection
系统 SHALL 在下载文件前，检查本地 `./client/load/` 目录是否已存在同名文件。

#### Scenario: Download Duplicate File
- **WHEN** 用户下载文件 `test2.txt`，且 `./client/load/test2.txt` 已存在
- **THEN** 客户端弹出弹窗显示 "file have exist"
- **AND** 中止本次下载，不创建/覆盖本地文件
- **AND** 用户点击 Close 关闭弹窗后可正常进行其它操作

#### Scenario: Download New File
- **WHEN** 用户下载文件 `test2.txt`，且 `./client/load/` 下无此文件
- **THEN** 正常执行下载

## MODIFIED Requirements

### Requirement: network_cmd_put
`network_cmd_put()` SHALL 在发送 PUT 指令前，提取 basename 并检查服务器 `./copy/` 目录下是否已存在同名文件。如果存在，弹出 "file have exist" 弹窗并返回 false。

### Requirement: start_download
`start_download()` SHALL 在 `open()` 创建本地文件前，检查 `./client/load/` 目录下是否已存在同名文件。如果存在，弹出 "file have exist" 弹窗、设置 `g_state = ST_IDLE` 并返回。

## REMOVED Requirements
无
