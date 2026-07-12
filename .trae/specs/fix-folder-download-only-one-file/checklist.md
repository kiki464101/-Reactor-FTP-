# Checklist

## 客户端下载流程调试日志
- [ ] `batch_start_next`中pop任务后添加日志输出文件名和is_upload标志
- [ ] `ST_WAIT_GET_RESP`处理中添加日志输出cmd_no、res_result、filesize
- [ ] `start_download`中添加日志输出dl_path、mkdir_p结果、open结果
- [ ] `finish_download`中添加日志输出g_batch_done、g_batch_total、g_tx_queue.count最终值
- [ ] `batch_check_complete`中添加日志确认batch完成条件

## 服务器端下载流程调试日志
- [ ] `worker_handle_get`中添加日志输出filename、path、stat结果
- [ ] `worker_handle_get`中文件打开失败时添加日志
- [ ] `listdir_recursive`中添加日志输出full_base和收集到的文件

## 客户端导航流程调试日志
- [ ] `on_file_item_clicked`中添加日志输出text、g_remote_cur_path
- [ ] `on_local_file_item_clicked`中添加日志输出text、g_local_cur_path
- [ ] `scan_local_directory`中添加日志输出full路径和opendir结果

## g_long_pressed标志修复
- [ ] `on_local_file_item_long_pressed`取消选中路径添加`g_long_pressed = true`
- [ ] `on_remote_file_item_long_pressed`取消选中路径添加`g_long_pressed = true`
- [ ] 长按取消选中后不再误触发导航

## start_download错误处理增强
- [ ] mkdir_p后添加stat检查确认目录创建成功
- [ ] open失败时添加perror日志输出errno

## 修复效果验证
- [ ] 服务端编译通过无错误
- [ ] 客户端编译通过无错误
- [ ] 下载包含多个文件的文件夹，所有文件都下载完成
- [ ] 下载后导航进入下载的文件夹，不出现"Unable file"
- [ ] 客户端多级子文件夹导航正常
- [ ] 服务器端多级子文件夹导航正常
- [ ] 长按选中/取消选中后单击行为正确
