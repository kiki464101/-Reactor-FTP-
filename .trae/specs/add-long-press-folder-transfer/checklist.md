# Checklist

## 服务端 LS 标记目录
- [x] `worker_handle_ls` 对每个 readdir 条目调用 stat 检查是否为目录
- [x] 目录条目添加 "/" 后缀（如 "subdir/"）
- [x] 文件条目无后缀（如 "a.txt"）
- [x] "." 和 ".." 仍被跳过
- [x] `worker_handle_ls` 仍在 worker 线程中执行（fd 独占，线程安全）

## 服务端 LISTDIR 命令（epoll + 线程池模式）
- [x] `protocol.h` 新增 `FTP_CMD_LISTDIR = 1033`
- [x] `thread_pool.h` 新增 `TASK_LISTDIR = 5`
- [x] `main.c` `dispatch_command` 新增 LISTDIR case
- [x] dispatch 时 `epoll_ctl(EPOLL_CTL_DEL)` 从 epoll 摘除 fd（保证 worker 独占）
- [x] dispatch 时 `thread_pool_submit(TASK_LISTDIR)` 提交到线程池
- [x] `handler.h` 声明 `worker_handle_listdir`
- [x] `worker_func` switch 新增 `TASK_LISTDIR` case
- [x] worker 完成后 `epoll_ctl(EPOLL_CTL_MOD)` rearm fd（复用现有逻辑）
- [x] `worker_handle_listdir` 递归遍历指定目录下所有文件
- [x] 递归辅助函数 `listdir_recursive` 正确实现
- [x] 跳过 "." 和 ".." 避免无限递归
- [x] 响应 payload 格式为 "path1\npath2\n"（相对路径含目录前缀）
- [x] 不存在的目录返回 result=0
- [x] `listdir_recursive` 只返回文件路径，不添加目录条目到响应（修复 bug）

## 客户端 UI 长按事件
- [x] 本地列表 button 注册 `LV_EVENT_LONG_PRESSED` 回调（与 CLICKED 并存）
- [x] 远程列表 button 注册 `LV_EVENT_LONG_PRESSED` 回调
- [x] 本地长按 ".." → 忽略（不选中不导航）
- [x] 本地长按文件夹（"/" 结尾）→ 选中（不导航）
- [x] 本地长按文件 → 选中（与单击一致）
- [x] 远程长按 "." 或 ".." → 忽略
- [x] 远程长按其它条目 → 选中
- [x] 本地单击文件夹仍能导航进入（单击行为不变）
- [x] 远程单击仍能选中（单击行为不变）

## 本地镜像数组改存目录
- [x] `ui_update_local_file_list_cb` 不再跳过目录条目
- [x] 目录条目以 "/" 后缀存入 `g_local_displayed_files[]`
- [x] 文件条目原样存入
- [x] 每次刷新前 count 重置为 0
- [x] 镜像数组仅 UI 线程写入，网络线程只读（单写者模式线程安全）

## 新查询函数
- [x] `ui_remote_list_has_entry(name)` 实现：检查 "name" 和 "name/" 两种形式
- [x] `ui_local_list_has_entry(name)` 实现：检查 "name" 和 "name/" 两种形式
- [x] 两个函数声明在 `ui_manager.h`
- [x] 传入 NULL 时安全返回 false

## 客户端上传文件夹遍历
- [x] `collect_files_recursive` 递归遍历本地文件夹
- [x] 跳过 "." 和 ".."
- [x] 子目录递归处理，rel_prefix 更新
- [x] 普通文件构建 task（filename = "rel_prefix/filename"，is_upload=true）
- [x] `folder_basename` 正确提取 basename（去掉尾部 "/" 后取最后一段）
- [x] `network_cmd_put_multi` 检测 "/" 结尾的目录条目
- [x] 目录 → 用 `ui_remote_list_has_entry(basename)` 检测重复
- [x] 重复 → 弹窗 "Dirent has exist"，`continue` 跳过
- [x] 不重复 → `collect_files_recursive` + 逐个 `tx_queue_push`
- [x] 文件条目保持现有逻辑
- [x] `tx_queue_push` 内部 `pthread_mutex_lock` 保护，UI 线程 push 与网络线程 pop 无竞争
- [x] 删除死代码 `check_file_exists`

## 客户端下载 LISTDIR 流程
- [x] `net_state_t` 新增 `ST_WAIT_LISTDIR_RESP`
- [x] `network_cmd_listdir` 构建命令包并发送
- [x] `network_cmd_get` 检测尾部 "/"，是目录则调用 `network_cmd_listdir`
- [x] 网络线程主循环处理 `ST_WAIT_LISTDIR_RESP` 状态
- [x] 解析响应 payload 中的文件列表（按 "\n" 分割，`strtok_r`）
- [x] 为每个文件入队 GET 任务（`tx_queue_push` mutex 保护）
- [x] `network_cmd_get_multi` 检测 "/" 结尾的目录条目
- [x] 目录 → 用 `ui_local_list_has_entry(basename)` 检测重复
- [x] 重复 → 弹窗 "Dirent has exist"，`continue` 跳过
- [x] 不重复 → 入队带 "/" 后缀的 GET 任务
- [x] `ST_WAIT_LISTDIR_RESP` 状态在网络线程主循环中正确处理，不与 poll 冲突
- [x] 清理 `network_cmd_get` 冗余 strncpy 代码

## 客户端 mkdir -p
- [x] `start_download` 检测 filename 是否含 "/"
- [x] 含 "/" → 用 `mkdir_p` 递归创建本地子目录
- [x] 子目录创建后正常 open + write 文件
- [x] `mkdir_p` 辅助函数实现正确

## 重复文件夹弹窗
- [x] 上传重复文件夹弹窗显示 "Dirent has exist"
- [x] 下载重复文件夹弹窗显示 "Dirent has exist"
- [x] 弹窗有 Close 按钮
- [x] 点击 Close 后弹窗销毁
- [x] 点击 Close 后可正常进行其它操作
- [x] 弹窗不重复创建（单例保护或复用 `ui_show_error_popup`）
- [x] 重复检测对 "name" 和 "name/" 两种形式都生效

## 进度条弹窗
- [x] 文件夹上传/下载时 `ui_show_progress_batch` 弹窗正常弹出
- [x] 进度条随传输进度更新
- [x] 进度条更新通过 `lv_async_call` 从网络线程异步到 UI 线程（线程安全）
- [x] Close 按钮：仅隐藏弹窗，传输继续
- [x] Close 后其它功能可正常运行
- [x] Cancel 按钮：调用 `network_cancel_transfer` 取消传输
- [x] Cancel 后弹窗关闭

## 线程安全审查
- [x] `g_tx_queue` 所有访问通过 `pthread_mutex_lock` 保护
- [x] `g_selected_local/remote` 仅 UI 线程访问
- [x] 镜像数组 `g_remote/local_displayed_files` 单写者模式（UI 线程写、网络线程读）
- [x] `g_state` 仅网络线程访问
- [x] `g_transfer_cancelled` 为 `volatile bool`，跨线程可见
- [x] `tx_queue_cancel_all` 使用 `pthread_cond_broadcast` 唤醒等待的 worker
- [x] 跨线程 UI 通信均通过 `lv_async_call`

## epoll 摘除/rearm 配对审查
- [x] LISTDIR 命令 dispatch 时 `epoll_ctl(EPOLL_CTL_DEL)` 摘除 fd
- [x] `worker_handle_listdir` 完成后 `worker_func` 复用现有 rearm 逻辑（`EPOLL_CTL_MOD`）
- [x] 摘除/rearm 配对完整，无 fd 泄漏

## 编译
- [x] 代码审查通过
- [ ] 服务端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 客户端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 无新增编译警告
