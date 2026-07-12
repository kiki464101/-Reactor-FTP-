# Tasks

- [ ] Task 1: 客户端下载流程添加调试日志
  - [ ] SubTask 1.1: `client/network_task.c` 在`batch_start_next`中，pop任务后添加日志输出文件名和is_upload标志
  - [ ] SubTask 1.2: `client/network_task.c` 在`ST_WAIT_GET_RESP`处理中，添加日志输出cmd_no、res_result、filesize（若成功）
  - [ ] SubTask 1.3: `client/network_task.c` 在`start_download`中，添加日志输出dl_path、mkdir_p结果、open结果
  - [ ] SubTask 1.4: `client/network_task.c` 在`finish_download`中，添加日志输出g_batch_done、g_batch_total、g_tx_queue.count的最终值
  - [ ] SubTask 1.5: `client/network_task.c` 在`batch_check_complete`中，添加日志确认batch完成条件

- [ ] Task 2: 服务器端下载流程添加调试日志
  - [ ] SubTask 2.1: `server/src/handler.c` 在`worker_handle_get`中，添加日志输出filename、path、stat结果
  - [ ] SubTask 2.2: `server/src/handler.c` 在`worker_handle_get`中，文件打开失败时添加日志
  - [ ] SubTask 2.3: `server/src/handler.c` 在`listdir_recursive`中，添加日志输出full_base和收集到的文件

- [ ] Task 3: 客户端导航流程添加调试日志
  - [ ] SubTask 3.1: `client/ui_manager.c` 在`on_file_item_clicked`中，添加日志输出text、g_remote_cur_path
  - [ ] SubTask 3.2: `client/ui_manager.c` 在`on_local_file_item_clicked`中，添加日志输出text、g_local_cur_path
  - [ ] SubTask 3.3: `client/ui_manager.c` 在`scan_local_directory`中，添加日志输出full路径和opendir结果

- [ ] Task 4: 修复g_long_pressed标志bug
  - [ ] SubTask 4.1: `client/ui_manager.c` 在`on_local_file_item_long_pressed`的取消选中路径（goto update_label前）添加`g_long_pressed = true`
  - [ ] SubTask 4.2: `client/ui_manager.c` 在`on_remote_file_item_long_pressed`的取消选中路径（goto update_label前）添加`g_long_pressed = true`

- [ ] Task 5: 修复start_download错误处理
  - [ ] SubTask 5.1: `client/network_task.c` 在`start_download`中，mkdir_p后添加stat检查确认目录创建成功
  - [ ] SubTask 5.2: `client/network_task.c` 在`start_download`中，open失败时添加perror日志输出errno

- [ ] Task 6: 验证修复效果
  - [ ] SubTask 6.1: 编译服务端和客户端代码无错误
  - [ ] SubTask 6.2: 测试下载包含多个文件的文件夹，确认所有文件下载完成
  - [ ] SubTask 6.3: 测试下载后导航进入下载的文件夹，不出现"Unable file"
  - [ ] SubTask 6.4: 测试多级子文件夹导航（客户端和服务器端）
  - [ ] SubTask 6.5: 测试长按选中/取消选中后单击行为正确

# Task Dependencies
- Task 2 独立（服务器端日志）
- Task 1 独立（客户端下载日志）
- Task 3 独立（客户端导航日志）
- Task 4 独立（g_long_pressed修复）
- Task 5 依赖 Task 1（在日志基础上增强错误处理）
- Task 6 依赖所有前置任务
