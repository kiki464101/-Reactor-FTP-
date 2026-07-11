# Delete Confirmation & Subdirectory Fix Spec

## Why
当前删除按钮点击后直接执行删除，缺乏二次确认，存在误删风险。同时，删除 `./client/load` 等子目录中的文件时路径拼接错误（子目录被重复拼接），导致删除失败。

## What Changes
- 新增删除确认弹窗（Yes/No 双按钮），点击 Yes 才执行删除，点击 No 取消并关闭弹窗
- **BREAKING** 修复子目录文件删除路径拼接 bug：`g_selected_local[i]` 已包含子目录前缀，删除时不应再拼接 `g_local_cur_path`

## Impact
- Affected specs: `add-delete-feature`（修改删除回调逻辑）
- Affected code:
  - `client/ui_manager.c` — 重构 `on_delete_btn_clicked`、新增确认弹窗和删除执行函数

## ADDED Requirements

### Requirement: Delete Confirmation Popup
系统 SHALL 在执行本地文件删除前，显示确认弹窗，包含 "Yes" 和 "No" 两个按钮。

#### Scenario: Click Yes
- **WHEN** 用户点击 Delete 按钮且选中了本地文件
- **THEN** 系统显示确认弹窗，提示 "Confirm delete?"
- **WHEN** 用户点击 Yes
- **THEN** 系统执行删除操作
- **AND** 弹窗关闭

#### Scenario: Click No
- **WHEN** 用户看到确认弹窗
- **AND** 用户点击 No
- **THEN** 弹窗关闭
- **AND** 不执行任何删除操作
- **AND** 选中状态保持不变（用户可以重新操作）

#### Scenario: Popup Singleton
- **WHEN** 确认弹窗已存在且有效
- **AND** 用户再次触发删除
- **THEN** 不重复创建弹窗

### Requirement: Correct Subdirectory Path in Deletion
系统 SHALL 使用 `g_selected_local[i]` 中已存储的完整相对路径（含子目录前缀）来构造删除路径，不再重复拼接 `g_local_cur_path`。

#### Scenario: Delete File in Subdirectory
- **WHEN** 用户在 `./client/load/` 子目录中选中文件 `haha.txt`
- **THEN** `g_selected_local[i]` = `"load/haha.txt"`
- **AND** 删除路径为 `./client/load/haha.txt`（正确）
- **AND** 文件被成功删除

#### Scenario: Delete File in Root
- **WHEN** 用户在 `./client/` 根目录中选中文件 `test.txt`
- **THEN** `g_selected_local[i]` = `"test.txt"`
- **AND** 删除路径为 `./client/test.txt`（正确）

## MODIFIED Requirements

### Requirement: Delete Button Handler
`on_delete_btn_clicked` 的逻辑 SHALL 变为：
1. 选中远程文件 → 显示 "error delete" 弹窗（不变）
2. 未选中本地文件 → 状态栏提示 "No file selected"（不变）
3. 选中本地文件 → 显示确认弹窗（**新增**，原先直接删除）
4. 确认弹窗 Yes → 执行删除（**新增**独立函数 `execute_local_delete()`）
5. 删除路径构造改为 `./client/%s`（**修复**，原先为 `./client/{g_local_cur_path}/{name}`）

**Previous**: 点击 Delete 直接执行删除，路径为 `./client/{g_local_cur_path}/{name}`
**New**: 点击 Delete 先弹确认窗，确认后执行删除，路径为 `./client/{name}`

## REMOVED Requirements
无
