# Fix Upload Filename Flatten Spec

## Why
上传子目录文件时，客户端发送的文件名包含子目录前缀（如 `test_delete1.txt/test2.txt`），服务器按此路径创建文件（`./copy/test_delete1.txt/test2.txt`），导致服务器端出现目录结构而非纯文件。用户期望服务器端只显示上传的文件名本身（如 `test2.txt`）。

## What Changes
- 客户端 `network_cmd_put()` 中，发送给服务器的文件名改为仅 basename（如 `test2.txt`），本地读取仍用完整路径

## Impact
- Affected code:
  - `client/network_task.c` — `network_cmd_put()` 提取 basename 发送给服务器
- 不需要修改的文件：
  - `server/src/handler.c` — 服务器按收到的文件名存储，收到的就是 basename，无需改
  - `client/ui_manager.c` — 选中逻辑不变

## ADDED Requirements
无

## MODIFIED Requirements

### Requirement: network_cmd_put
`network_cmd_put()` SHALL 发送给服务器的文件名仅为 basename（去掉子目录前缀），但本地文件读取仍使用完整路径。

**Previous**: `build_cmd_put(filename, ...)` 发送完整路径如 `test_delete1.txt/test2.txt`
**New**: `build_cmd_put(basename, ...)` 仅发送 `test2.txt`

#### Scenario: Upload File in Subdirectory
- **WHEN** 客户端上传 `test_delete1.txt/test2.txt`
- **THEN** 本地读取路径为 `./client/test_delete1.txt/test2.txt`（完整路径）
- **AND** 发送给服务器的文件名为 `test2.txt`（basename）
- **AND** 服务器在 `./copy/test2.txt` 存储文件

#### Scenario: Upload File in Root
- **WHEN** 客户端上传 `hello.txt`（无子目录前缀）
- **THEN** 发送给服务器的文件名为 `hello.txt`
- **AND** 服务器在 `./copy/hello.txt` 存储文件

## REMOVED Requirements
无
