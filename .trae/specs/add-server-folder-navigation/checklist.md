# Checklist

## 服务端路径遍历防护
- [ ] `is_path_safe(const char *path)` 静态辅助函数已实现
- [ ] 检查 path 不为 NULL
- [ ] 检查 path 不以 "/" 开头（非绝对路径）
- [ ] 检查 path 不含 ".." 子串
- [ ] `worker_handle_ls` 调用 `is_path_safe` 检查路径参数
- [ ] `worker_handle_get` 调用 `is_path_safe` 检查 filename
- [ ] `worker_handle_listdir` 调用 `is_path_safe` 检查 dirname
- [ ] 不安全路径返回 result=0

## 服务端 LS 路径参数
- [ ] `handler.h` 中 `worker_handle_ls` 签名改为 `(sess, payload, plen)`
- [ ] `handler.c` 中 `worker_handle_ls` 检测 `plen < 12` 时列出根目录（向后兼容）
- [ ] `handler.c` 中 `worker_handle_ls` 解析 payload 中的路径参数
- [ ] `handler.c` 中 `worker_handle_ls` 调用 `is_path_safe` 检查路径
- [ ] 构建 `MY_FTP_BOOT/path` 路径并 opendir
- [ ] 目录条目添加 "/" 后缀（保持现有逻辑）
- [ ] 路径不存在时返回 result=0
- [ ] `worker_func` 中 `TASK_LS` case 调用 `worker_handle_ls(sess, task.payload, task.payload_len)`

## 客户端 network_cmd_ls 路径参数
- [ ] `network_task.h` 中 `network_cmd_ls` 签名改为 `(const char *path)`
- [ ] `network_task.c` 中 path 为 NULL/空时 `build_cmd(FTP_CMD_LS, NULL, 0, &len)`
- [ ] `network_task.c` 中 path 非空时 `build_cmd_with_str(FTP_CMD_LS, path, &len)`
- [ ] 命令包正确发送

## 客户端远程路径跟踪
- [ ] `g_remote_cur_path[256]` 全局变量已定义
- [ ] `on_file_item_clicked` 开头有 `g_long_pressed` 抑制检查
- [ ] `g_long_pressed` 为 true 时重置为 false 并返回（不导航不选中）
- [ ] 单击 ".." → 截断 `g_remote_cur_path` 最后一段 + 调用 `network_cmd_ls(g_remote_cur_path)`
- [ ] 单击文件夹（"/" 结尾）→ 追加到 `g_remote_cur_path` + 调用 `network_cmd_ls(g_remote_cur_path)`
- [ ] 单击普通文件 → 切换选中，存储完整路径 `g_remote_cur_path/filename`
- [ ] 根目录时选中存储仅 `filename`（无前缀）
- [ ] 取消选中时匹配完整路径
- [ ] `on_remote_file_item_long_pressed` 选中时设置 `g_long_pressed = true`
- [ ] `on_remote_file_item_long_pressed` 选中时存储完整路径
- [ ] `on_remote_file_item_long_pressed` 取消选中时匹配完整路径
- [ ] `on_refresh_btn_clicked` 重置 `g_remote_cur_path` 为空
- [ ] `on_refresh_btn_clicked` 调用 `network_cmd_ls(NULL)`
- [ ] 登录成功后的初始 LS 调用 `network_cmd_ls(NULL)`

## 客户端远程列表 ".." 条目
- [ ] `ui_update_file_list_cb` 在 `g_remote_cur_path` 非空时添加路径标题
- [ ] `ui_update_file_list_cb` 在 `g_remote_cur_path` 非空时添加 ".." 条目
- [ ] ".." 条目注册 `on_file_item_clicked` 回调
- [ ] ".." 条目注册 `on_remote_file_item_long_pressed` 回调
- [ ] ".." 条目不存入 `g_remote_displayed_files[]` 镜像数组
- [ ] 根目录时不显示 ".." 和路径标题

## g_long_pressed 抑制逻辑验证
- [ ] 远程长按文件夹 → 设置 `g_long_pressed = true`
- [ ] 长按后的 CLICKED 事件被抑制（不导航不选中）
- [ ] 抑制后 `g_long_pressed` 重置为 false
- [ ] 非长按的单击正常触发导航或选中

## 子目录下载兼容性验证
- [ ] 在子目录中选中文件 → `g_selected_remote` 存储完整路径
- [ ] 下载时 GET 命令发送完整路径（如 "testfolder/file.txt"）
- [ ] 服务端 `worker_handle_get` 构建 `./copy/testfolder/file.txt` 路径
- [ ] 路径遍历检查通过（不含 ".."）
- [ ] `start_download` 的 `mkdir_p` 创建本地子目录 `./client/load/testfolder/`
- [ ] 文件写入 `./client/load/testfolder/file.txt`
- [ ] `network_cmd_get_multi` 重复检测 `folder_basename` 正确提取 basename
- [ ] LISTDIR 处理含前缀路径（如 "subdir/testfolder/"）正确

## 导航行为验证
- [ ] 单击远程文件夹 → 导航进入子目录
- [ ] 子目录列表显示文件和子目录（带 "/" 后缀）
- [ ] 子目录列表显示 ".." 条目
- [ ] 单击 ".." → 返回上级目录
- [ ] 单击远程文件 → 切换选中（不导航）
- [ ] 长按远程文件夹 → 选中（不导航）
- [ ] 长按远程文件 → 选中

## 上传文件夹后导航验证
- [ ] 上传 client/test 文件夹 → server 端创建 ./copy/test/ 目录
- [ ] 刷新远程列表 → 显示 "test/" 条目
- [ ] 单击 "test/" → 导航进入，显示 test 文件夹下的所有文件

## 路径遍历防护验证
- [ ] LS 路径 "../" → 拒绝 (result=0)
- [ ] GET 文件名 "../etc/passwd" → 拒绝 (result=0)
- [ ] LISTDIR 路径 "../secret" → 拒绝 (result=0)
- [ ] 正常子目录路径 "testfolder/sub" → 正常列出

## 线程安全
- [ ] `g_remote_cur_path` 仅 UI 线程访问
- [ ] `g_long_pressed` 仅 UI 线程访问（UI 事件回调中读写）
- [ ] `network_cmd_ls` 在 UI 线程构建和发送命令
- [ ] LS 响应通过 `lv_async_call` 异步回调 UI 线程

## 编译
- [ ] 代码审查通过
- [ ] 服务端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 客户端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 无新增编译警告
