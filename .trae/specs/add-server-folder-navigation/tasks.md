# Tasks

- [ ] Task 1: 服务端路径遍历防护 + LS 命令支持路径参数
  - [ ] SubTask 1.1: `server/src/handler.c` 新增静态辅助函数 `is_path_safe(const char *path)`：
    - 检查 path 不为 NULL
    - 检查 path 不以 "/" 开头（非绝对路径）
    - 检查 path 不含 ".." 段（用 strstr 检查 ".." 子串，或逐段解析）
    - 返回 true（安全）或 false（危险）
  - [ ] SubTask 1.2: `server/inc/handler.h` 修改 `worker_handle_ls` 声明为 `void worker_handle_ls(client_session_t *sess, const unsigned char *payload, int plen);`
  - [ ] SubTask 1.3: `server/src/handler.c` 修改 `worker_handle_ls` 实现：
    - 函数签名改为 `(client_session_t *sess, const unsigned char *payload, int plen)`
    - 检测 `plen < 12`（无路径参数）→ 列出 `MY_FTP_BOOT` 根目录（保持现有逻辑）
    - 检测 `plen >= 12` → 解析 `[4B cmd_no][4B path_len][path]`
    - 调用 `is_path_safe(path)` 检查路径安全性，不安全则 `tx(sess, FTP_CMD_LS, 0, ...)`
    - 构建 `MY_FTP_BOOT/path` 路径，opendir + readdir 遍历，stat 检查目录添加 "/" 后缀
    - 路径不存在或不是目录 → `tx(sess, FTP_CMD_LS, 0, ...)`
  - [ ] SubTask 1.4: `server/src/handler.c` 修改 `worker_handle_get`：
    - 在解析 filename 后、构建 path 前，调用 `is_path_safe(filename)` 检查
    - 不安全则 `tx(sess, FTP_CMD_GET, 0, ...)` 并返回
  - [ ] SubTask 1.5: `server/src/handler.c` 修改 `worker_handle_listdir`：
    - 在解析 dirname 后、构建 dir_path 前，调用 `is_path_safe(dirname)` 检查
    - 不安全则 `tx(sess, FTP_CMD_LISTDIR, 0, ...)` 并返回
  - [ ] SubTask 1.6: `server/src/handler.c` 修改 `worker_func` 中 `TASK_LS` case：`worker_handle_ls(sess, task.payload, task.payload_len);`

- [ ] Task 2: 客户端 network_cmd_ls 支持路径参数
  - [ ] SubTask 2.1: `client/network_task.h` 修改 `network_cmd_ls` 声明为 `bool network_cmd_ls(const char *path);`
  - [ ] SubTask 2.2: `client/network_task.c` 修改 `network_cmd_ls` 实现：
    - 签名改为 `bool network_cmd_ls(const char *path)`
    - path 为 NULL 或空字符串 → `build_cmd(FTP_CMD_LS, NULL, 0, &len)`（无参数，列出根目录）
    - path 非空 → `build_cmd_with_str(FTP_CMD_LS, path, &len)` 构建带路径的命令包
    - 发送命令包

- [ ] Task 3: 客户端 UI 远程路径跟踪 + 导航逻辑 + g_long_pressed 修复
  - [ ] SubTask 3.1: `client/ui_manager.c` 新增全局变量 `static char g_remote_cur_path[256] = {0};`（放在 `g_local_cur_path` 附近）
  - [ ] SubTask 3.2: `client/ui_manager.c` 修改 `on_file_item_clicked`（远程单击）：
    - 开头添加 `g_long_pressed` 抑制检查：若 `g_long_pressed` 为 true，重置为 false 并返回（与 `on_local_file_item_clicked` 一致）
    - ".." → 截断 `g_remote_cur_path` 最后一段（用 strrchr 找 '/'），调用 `network_cmd_ls(g_remote_cur_path)` 刷新
    - 以 "/" 结尾 → 去掉尾部 "/"，追加到 `g_remote_cur_path`（若 cur_path 非空则用 "/" 连接），调用 `network_cmd_ls(g_remote_cur_path)` 刷新
    - 普通文件 → 切换选中，存储完整路径：
      - 若 `g_remote_cur_path[0]` 非空：`snprintf(full_path, "%s/%s", g_remote_cur_path, text)`
      - 若为空：`strncpy(full_path, text, ...)`
      - 存入 `g_selected_remote`，与取消选中时匹配完整路径
  - [ ] SubTask 3.3: `client/ui_manager.c` 修改 `on_remote_file_item_long_pressed`：
    - 选中时设置 `g_long_pressed = true`（与 `on_local_file_item_long_pressed` 一致）
    - 选中时存储完整路径（与 `on_file_item_clicked` 文件选中逻辑一致）：
      - 文件夹：`g_remote_cur_path/foldername/`（保留尾部 "/"）
      - 普通文件：`g_remote_cur_path/filename`
    - 取消选中时匹配完整路径
  - [ ] SubTask 3.4: `client/ui_manager.c` 修改 `on_refresh_btn_clicked`：
    - 重置 `g_remote_cur_path[0] = '\0';`
    - 调用 `network_cmd_ls(NULL)` 代替 `network_cmd_ls()`
  - [ ] SubTask 3.5: `client/ui_manager.c` 修改初始连接后的 LS 调用（`ui_switch_to_main` 或登录成功后的刷新）：
    - 调用 `network_cmd_ls(NULL)` 代替 `network_cmd_ls()`

- [ ] Task 4: 客户端 UI 远程列表 ".." 条目 + 路径标题
  - [ ] SubTask 4.1: `client/ui_manager.c` 修改 `ui_update_file_list_cb`：
    - 在清除旧条目后、解析 filelist 前，检查 `g_remote_cur_path` 是否非空
    - 非空时添加路径标题 `lv_list_add_text(main_file_list, "Path: <g_remote_cur_path>")`
    - 非空时添加 ".." 条目 `lv_list_add_button(main_file_list, NULL, "..")`，注册 `on_file_item_clicked`（CLICKED）和 `on_remote_file_item_long_pressed`（LONG_PRESSED）回调
  - [ ] SubTask 4.2: 确认 ".." 条目不存入 `g_remote_displayed_files[]` 镜像数组（与本地列表 ".." 处理一致）

- [ ] Task 5: 子目录路径兼容性验证 + 编译
  - [ ] SubTask 5.1: 验证 `start_download` 处理含 "/" 的 filename：
    - 路径拼接 `./client/load/` + "testfolder/file.txt" → `./client/load/testfolder/file.txt`
    - `mkdir_p` 创建 `./client/load/testfolder/` 子目录
    - `open` 创建文件
  - [ ] SubTask 5.2: 验证 `network_cmd_get_multi` 重复检测：
    - `folder_basename("testfolder/file.txt")` → "file.txt"
    - `ui_local_list_has_entry("file.txt")` 正确检测
  - [ ] SubTask 5.3: 验证 LISTDIR 处理含前缀路径（如 "subdir/testfolder/"）
  - [ ] SubTask 5.4: 确认单击远程文件夹导航进入子目录，显示子目录内容和 ".."
  - [ ] SubTask 5.5: 确认单击 ".." 返回上级目录
  - [ ] SubTask 5.6: 确认单击远程文件切换选中，存储完整路径
  - [ ] SubTask 5.7: 确认长按远程文件夹选中（不导航），设置 g_long_pressed
  - [ ] SubTask 5.8: 确认长按后单击被抑制（不导航不选中）
  - [ ] SubTask 5.9: 确认下载子目录中的文件时 GET 命令发送完整路径
  - [ ] SubTask 5.10: 确认刷新按钮重置远程路径到根目录
  - [ ] SubTask 5.11: 确认上传文件夹到 server 端后，可以单击进入该文件夹查看内容
  - [ ] SubTask 5.12: 确认路径遍历防护：LS/GET/LISTDIR 拒绝 ".." 路径
  - [ ] SubTask 5.13: 服务端编译通过无错误（用户在 Linux 虚拟机验证）
  - [ ] SubTask 5.14: 客户端编译通过无错误（用户在 Linux 虚拟机验证）

# Task Dependencies
- Task 1 独立（服务端修改）
- Task 2 独立（客户端网络层修改）
- Task 3 依赖 Task 2（UI 调用 `network_cmd_ls(path)`）
- Task 4 依赖 Task 3（".." 条目需导航逻辑就绪）
- Task 5 依赖所有前置任务

# 并行执行建议
- Task 1 + Task 2 可并行（服务端和客户端网络层修改无依赖）
- Task 3 + Task 4 必须串行（都修改 ui_manager.c，且 Task 4 依赖 Task 3 的路径跟踪）
