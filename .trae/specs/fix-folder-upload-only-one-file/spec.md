# 修复文件夹上传只传一个文件问题 Spec

## Why
用户长按test文件夹后点击upload，只能上传yayay.txt一个文件，而不是整个文件夹。问题出在`collect_files_recursive`函数递归收集文件时，可能没有正确遍历文件夹下的所有文件。

## What Changes
- 在`collect_files_recursive`函数中添加调试日志，输出扫描目录和收集到的文件
- 检查`opendir`是否成功打开目录
- 检查`readdir`是否正确遍历所有条目
- 确认递归调用时`rel_prefix`参数正确传递

## Impact
- Affected specs: add-long-press-folder-transfer
- Affected code: `client/network_task.c` 中的 `collect_files_recursive` 函数

## ADDED Requirements
### Requirement: 文件夹上传完整遍历
系统 SHALL 在上传文件夹时，递归遍历文件夹下的所有文件和子文件夹，并将所有文件入队上传。

#### Scenario: 上传包含多个文件的文件夹
- **WHEN** 用户长按选中test文件夹（包含yayay.txt和其他文件）
- **THEN** 系统应收集test文件夹下所有文件并逐个上传
- **AND** 服务器端应显示完整的test文件夹结构

## MODIFIED Requirements
### Requirement: collect_files_recursive 调试支持
函数 SHALL 在关键位置添加调试日志，帮助定位问题：
- 输出扫描的目录路径
- 输出收集到的每个文件名
- 输出递归调用时的rel_prefix参数

## 问题分析
从代码分析，可能的原因包括：
1. `opendir`打开目录失败，导致函数直接返回
2. `readdir`遍历过程中出现问题
3. 递归调用时参数传递错误
4. `MAX_SELECTED_FILES`限制导致提前退出

## 解决方案
1. 在`collect_files_recursive`函数入口添加日志，输出`scan_dir`路径
2. 在`opendir`失败时添加错误日志
3. 在收集每个文件时添加日志，输出文件名
4. 在递归调用时添加日志，输出`rel_prefix`参数
5. 检查`MAX_SELECTED_FILES`的值是否足够大
