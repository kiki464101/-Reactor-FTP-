# 修复文件夹下载和子文件夹导航问题 Spec

## Why
用户遇到三个关联问题：
1. 长按hahahah/文件夹下载，LISTDIR正确解析了2个文件并入队，但只下载了第一个文件（henghengheng.txt），第二个文件（siusiusiu.txt）启动后无完成日志
2. 下载完成后在client/load下点击下载好的文件夹显示"Unable file"（opendir失败）
3. 客户端和服务器端都存在子文件夹导航bug：只能打开第一层文件夹，第二层文件夹点击无反应或报错

## What Changes
- 在`finish_download`、`batch_start_next`、`ST_WAIT_GET_RESP`处理中添加调试日志，定位第二个文件下载失败原因
- 在`start_download`中添加日志确认目录创建和文件打开结果
- 在`on_file_item_clicked`和`on_local_file_item_clicked`中添加日志，定位子文件夹导航失败原因
- 在`scan_local_directory`和`worker_handle_ls`中添加日志，确认嵌套路径opendir结果
- 修复`g_long_pressed`标志在取消选中时未设置的bug（导致长按取消选中后误触发导航）

## Impact
- Affected specs: add-long-press-folder-transfer, fix-folder-download-and-popup
- Affected code:
  - `client/network_task.c`: `finish_download`、`batch_start_next`、`ST_WAIT_GET_RESP`处理、`start_download`
  - `client/ui_manager.c`: `on_file_item_clicked`、`on_local_file_item_clicked`、`scan_local_directory`、`on_local_file_item_long_pressed`、`on_remote_file_item_long_pressed`
  - `server/src/handler.c`: `worker_handle_ls`、`worker_handle_get`、`listdir_recursive`

## ADDED Requirements
### Requirement: 文件夹下载完整执行
系统 SHALL 在下载文件夹时，通过LISTDIR获取的所有文件都必须被下载完成，每个文件的下载结果（成功/失败）都应有日志记录。

#### Scenario: 下载包含多个文件的文件夹
- **WHEN** 用户长按选中hahahah/文件夹（包含henghengheng.txt和siusiusiu.txt）
- **THEN** LISTDIR返回2个文件路径
- **AND** 2个文件都被入队下载
- **AND** 每个文件下载完成后都有finish_download日志
- **AND** 下载完成后client/load/hahahah/目录下包含所有文件

### Requirement: 子文件夹导航正常
系统 SHALL 支持多级文件夹导航，用户可以点击第一层文件夹进入，也可以点击第二层及更深层文件夹进入。

#### Scenario: 导航进入子文件夹
- **WHEN** 用户在client/load/目录下点击hahahah/文件夹
- **THEN** 本地列表刷新显示hahahah/文件夹下的内容
- **AND** 不出现"Unable file"错误

#### Scenario: 服务器端子文件夹导航
- **WHEN** 用户在服务器根目录点击hahahah/文件夹
- **THEN** 服务器返回hahahah/文件夹下的内容
- **AND** 用户可以继续点击hahahah/下的子文件夹进入

## MODIFIED Requirements
### Requirement: g_long_pressed标志一致性
长按事件处理器 SHALL 在所有处理路径（选中、取消选中、忽略）都正确设置`g_long_pressed`标志，确保后续的CLICKED事件被正确抑制或放行。

## 问题分析

### 问题1：第二个文件下载失败
从终端日志：
```
[listdir resp] parsed 2 files, batch_total=2 queue=2
[DEBUG] main loop: starting next queued transfer (queue=2)
[DEBUG] finish_download: hahahah/henghengheng.txt success=1 batch=1 done=0/2 queue=1
[DEBUG] main loop: starting next queued transfer (queue=1)
```
第二个文件"siusiusiu.txt"的GET命令已发送，但没有finish_download日志。可能原因：
1. 服务器返回`res_result=0`（文件不存在）→ 客户端显示"Server: file not found"，但`g_batch_done++`后队列空，batch结束
2. 服务器返回成功但`start_download`创建文件失败
3. 下载过程中网络错误
4. 响应包解析失败

### 问题2：下载的文件夹无法打开
"Unable file"来自`ui_update_local_file_list_cb`，当`scan_local_directory`返回NULL时（opendir失败）。可能原因：
1. `./client/load/hahahah/`目录未被创建（第一个文件下载失败时mkdir_p未执行）
2. 目录权限问题
3. 路径拼接错误

### 问题3：子文件夹导航失败
可能原因：
1. `g_long_pressed`标志在取消选中时未设置，导致长按取消选中后CLICKED事件触发导航
2. 嵌套路径opendir失败（目录不存在）
3. 服务器LS对嵌套路径返回失败
4. 路径拼接buffer溢出

### 问题4：g_long_pressed bug
在`on_local_file_item_long_pressed`和`on_remote_file_item_long_pressed`中：
- 选中（toggle on）路径：设置`g_long_pressed = true` ✓
- 取消选中（toggle off）路径：未设置`g_long_pressed` ✗
- 忽略（".."）路径：未设置`g_long_pressed` ✗（但这是故意的，允许导航）

取消选中路径不设置`g_long_pressed`会导致：长按取消选中后，CLICKED事件未被抑制，触发导航进入文件夹。

## 解决方案
1. 在下载流程关键位置添加日志：`batch_start_next`、`ST_WAIT_GET_RESP`、`start_download`、`finish_download`
2. 在导航流程添加日志：`on_file_item_clicked`、`on_local_file_item_clicked`、`scan_local_directory`
3. 在服务器端添加日志：`worker_handle_ls`、`worker_handle_get`、`listdir_recursive`
4. 修复`g_long_pressed`：在所有长按处理路径末尾设置`g_long_pressed = true`（除了".."忽略路径）
5. 确保`start_download`中`mkdir_p`和`open`失败时有错误日志
