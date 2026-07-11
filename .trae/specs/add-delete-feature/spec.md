# Delete Feature Spec

## Why
用户需要能够删除本地文件，同时防止误删服务器文件。当用户尝试删除远程文件时，应显示 "error delete" 弹窗，点击 Close 关闭后可正常进行其它操作。

## What Changes
- 在客户端主界面按钮行添加 "Delete" 按钮（位于 "Upload" 和 "Disconnect" 之间）
- 实现本地文件删除功能（仅允许删除 client 自己的文件）
- 实现远程文件删除拦截（显示 "error delete" 弹窗）
- 弹窗关闭后恢复正常操作
- 拦截对目录的删除操作（仅允许删除普通文件）

## Impact
- Affected code:
  - `client/ui_manager.c` — 添加 Delete 按钮、事件回调、删除逻辑
- 不需要修改的文件：
  - `client/ui_manager.h` — 无新增对外接口
  - `client/network_task.c/h` — 纯本地操作，不涉及网络
  - `server/` 下所有文件 — 不涉及服务端

## ADDED Requirements

### Requirement: Delete Button in Main Screen
系统 SHALL 在主界面按钮行添加 "Delete" 按钮，位于 "Upload" 和 "Disconnect" 之间。
按钮宽度 90，高度 34，使用已有 `create_btn()` 函数创建。

#### Scenario: Delete Local File
- **WHEN** 用户仅选择本地文件（Local Files）并点击 Delete 按钮
- **THEN** 系统删除选中的本地文件
- **AND** 刷新本地文件列表
- **AND** 显示成功提示 "Deleted"

#### Scenario: Delete Remote File (Forbidden)
- **WHEN** 用户选择了远程文件（Remote Files，无论是否同时选了本地文件）并点击 Delete 按钮
- **THEN** 系统显示 "error delete" 弹窗
- **AND** 不执行任何删除操作

#### Scenario: No File Selected
- **WHEN** 用户未选择任何文件（远程和本地均无选中）并点击 Delete 按钮
- **THEN** 系统在状态栏显示 "No file selected" 提示

#### Scenario: Close Error Popup
- **WHEN** 用户看到 "error delete" 弹窗
- **AND** 点击 Close 按钮
- **THEN** 弹窗关闭（`lv_obj_del` 销毁弹窗对象）
- **AND** `err_popup` 和 `err_label` 指针置 NULL
- **AND** 用户可以正常进行其它操作（弹窗不阻塞主循环）

#### Scenario: Delete Directory (Forbidden)
- **WHEN** 用户选中的本地条目是目录（文件名以 "/" 结尾）
- **THEN** 跳过该条目，不执行删除
- **AND** 不影响其它选中文件的删除

### Requirement: Local File Deletion
系统 SHALL 能够删除 `./client/` 目录下的文件，包括子目录中的文件。

#### Scenario: Successful Deletion
- **WHEN** 系统执行本地文件删除
- **THEN** 文件从文件系统中移除（调用 `remove()`）
- **AND** 本地文件列表刷新后不再显示该文件

#### Scenario: Path Construction
- **WHEN** 当前在子目录中浏览（`g_local_cur_path` 非空）
- **THEN** 文件完整路径为 `./client/{g_local_cur_path}/{filename}`
- **WHEN** 当前在根目录浏览（`g_local_cur_path` 为空）
- **THEN** 文件完整路径为 `./client/{filename}`

#### Scenario: Partial Failure
- **WHEN** 批量删除中部分文件失败（如权限不足）
- **THEN** 成功的文件正常删除
- **AND** 状态栏显示 "Some files failed to delete"

### Requirement: Multi-file Deletion
系统 SHALL 支持同时删除多个选中的本地文件。

#### Scenario: Delete Multiple Files
- **WHEN** 用户选择多个本地文件并点击 Delete
- **THEN** 所有选中的普通文件被删除
- **AND** 本地文件列表刷新
- **AND** 选中状态清空

## MODIFIED Requirements

### Requirement: Button Layout
主界面按钮行 SHALL 包含 5 个按钮：Refresh, Download, Upload, Delete, Disconnect。

**Previous**: 4 个按钮 (Refresh, Download, Upload, Disconnect)
**New**: 5 个按钮 (Refresh, Download, Upload, Delete, Disconnect)

## REMOVED Requirements
无
