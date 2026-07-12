# Tasks

- [ ] Task 1: 修复状态机竞态条件（核心bug，立即生效）
  - [ ] SubTask 1.1: `client/network_task.c` 第1016-1028行`ST_WAIT_GET_RESP`的else分支：检查`rsp2.cmd_no == FTP_CMD_LS`则交给LS处理逻辑（复制第1090-1104行的LS响应处理），不递增`g_batch_done`
  - [ ] SubTask 1.2: `client/network_task.c` 第1030-1047行`ST_WAIT_PUT_RESP`的else分支：同样检查`FTP_CMD_LS`和`FTP_CMD_DONE`
  - [ ] SubTask 1.3: `client/network_task.c` 第1049-1086行`ST_WAIT_LISTDIR_RESP`的else分支：同样检查`FTP_CMD_LS`和`FTP_CMD_DONE`
  - [ ] SubTask 1.4: 提取LS响应处理为独立函数`handle_ls_response(resp_t *rsp)`，避免代码重复
  - [ ] SubTask 1.5: 验证下载过程中点击文件夹导航，子文件夹内容正常显示

- [ ] Task 2: 增强transfer_worker_t结构体
  - [ ] SubTask 2.1: `client/network_task.h` 修改`transfer_worker_t`，新增字段：`bool busy`、`int sockfd`、`int progress_percent`、`char current_filename[256]`、`bool is_upload`、`pthread_mutex_t mutex`
  - [ ] SubTask 2.2: `client/network_task.c` 初始化`g_tx_workers`数组，每个worker的mutex初始化

- [ ] Task 3: 实现worker线程独立连接和登录
  - [ ] SubTask 3.1: `client/network_task.c` 新增`worker_connect_and_login(transfer_worker_t *w)`函数：
    - 创建socket：`w->sockfd = socket(AF_INET, SOCK_STREAM, 0)`
    - 连接服务器（使用`g_login_ip`/`g_login_port`）
    - 发送LOGIN命令，等待响应
    - 返回成功/失败
  - [ ] SubTask 3.2: 添加错误处理：连接失败或登录失败时返回false

- [ ] Task 4: 实现worker下载函数
  - [ ] SubTask 4.1: `client/network_task.c` 新增`worker_do_download(transfer_worker_t *w, const char *filename)`函数：
    - 发送GET命令（`build_cmd_with_str`，用`w->sockfd`）
    - 等待GET响应（`read_packet`+`parse_response`，用`w->sockfd`）
    - 解析filesize
    - `mkdir_p`创建本地子目录（`./client/load/<dir>/`）
    - `open`创建本地文件
    - 循环`read`socket数据，`write`到文件
    - 每chunk后`usleep(CHUNK_DELAY_US)`保证进度条可见
    - 每10%通过`lv_async_call`更新进度
    - 检查`g_transfer_cancelled`，若取消则关闭文件并删除
    - 完成后消耗`FTP_CMD_DONE`包
    - 关闭文件
  - [ ] SubTask 4.2: 添加日志：下载开始、进度、完成/失败

- [ ] Task 5: 实现worker上传函数
  - [ ] SubTask 5.1: `client/network_task.c` 新增`worker_do_upload(transfer_worker_t *w, const char *filename)`函数：
    - `normalize_local_path`构建本地路径
    - `stat`获取文件大小
    - `open`本地文件
    - 发送PUT命令（`build_cmd_put`含filename和filesize，用`w->sockfd`）
    - 等待PUT ACK（用`w->sockfd`）
    - 循环`read`文件，`write`到socket
    - 每10%通过`lv_async_call`更新进度
    - 检查`g_transfer_cancelled`
    - 完成后消耗`FTP_CMD_DONE`包
    - 关闭文件
  - [ ] SubTask 5.2: 添加日志：上传开始、进度、完成/失败

- [ ] Task 6: 实现worker线程主循环
  - [ ] SubTask 6.1: `client/network_task.c` 新增`transfer_worker_thread_func(void *arg)`函数：
    - 循环：`tx_queue_pop`取任务（阻塞等待）
    - 检查`g_network_running`，若false则退出
    - 若`w->sockfd < 0`，调用`worker_connect_and_login`
    - 设置`w->busy = true`，`w->current_filename`
    - 若`task.is_upload`→`worker_do_upload`，否则→`worker_do_download`
    - 设置`w->busy = false`
    - `g_batch_done++`（mutex保护）
    - 检查batch完成：`if (g_batch_done >= g_batch_total)`→`lv_async_call`通知UI
  - [ ] SubTask 6.2: worker退出时关闭socket

- [ ] Task 7: 实现线程池生命周期管理
  - [ ] SubTask 7.1: `client/network_task.c` 实现`transfer_pool_init()`：
    - 遍历`g_tx_workers`数组
    - 初始化每个worker的mutex
    - `pthread_create`创建worker线程
    - 设置`w->running = true`
  - [ ] SubTask 7.2: `client/network_task.c` 实现`transfer_pool_stop()`：
    - 设置`g_network_running = false`
    - `tx_queue_cancel_all`（cond_broadcast唤醒所有等待的worker）
    - 遍历`g_tx_workers`，`pthread_join`每个线程
    - 销毁mutex
  - [ ] SubTask 7.3: 在`network_thread_func`登录成功后调用`transfer_pool_init`
  - [ ] SubTask 7.4: 在`network_thread_func`退出前调用`transfer_pool_stop`

- [ ] Task 8: 修改network_cmd_get_multi和network_cmd_put_multi
  - [ ] SubTask 8.1: `client/network_task.c` 修改`network_cmd_get_multi`：
    - 文件夹下载：发送LISTDIR请求（用主socket）
    - 等待LISTDIR响应，解析文件列表
    - 将每个文件任务`tx_queue_push`到队列
    - `pthread_cond_broadcast`通知所有worker
    - 不再调用`batch_start_next`（worker线程自己pop）
  - [ ] SubTask 8.2: `client/network_task.c` 修改`network_cmd_put_multi`：
    - 文件夹上传：`collect_files_recursive`收集文件
    - 将每个文件任务`tx_queue_push`到队列
    - `pthread_cond_broadcast`通知所有worker
  - [ ] SubTask 8.3: 移除主网络线程中的`batch_start_next`串行调度逻辑（改由worker并发）

- [ ] Task 9: 简化主网络线程（专用于命令通道）
  - [ ] SubTask 9.1: `client/network_task.c` 主网络线程不再处理ST_DOWNLOADING/ST_UPLOADING（由worker处理）
  - [ ] SubTask 9.2: 主网络线程只处理：LOGIN、LS、LISTDIR请求和响应
  - [ ] SubTask 9.3: 保留Task 1的状态机修复作为fallback（防止worker未启动时的竞态）

- [ ] Task 10: UI进度条多任务支持
  - [ ] SubTask 10.1: `client/ui_manager.c` 修改`ui_show_progress_batch`，为每个worker创建独立进度条
  - [ ] SubTask 10.2: `client/ui_manager.c` 新增`cb_worker_progress`回调，更新指定worker的进度条
  - [ ] SubTask 10.3: `client/network_task.c` 进度更新回调携带worker ID

- [ ] Task 11: 验证和测试
  - [ ] SubTask 11.1: 编译服务端和客户端代码无错误
  - [ ] SubTask 11.2: 测试下载包含多个文件的文件夹，所有文件都下载完成
  - [ ] SubTask 11.3: 测试多级子文件夹导航（客户端和服务器端），可打开任意层级
  - [ ] SubTask 11.4: 测试下载过程中点击文件夹导航，正常显示子文件夹内容
  - [ ] SubTask 11.5: 测试并发下载多个文件，进度条独立显示
  - [ ] SubTask 11.6: 测试取消传输，所有worker停止
  - [ ] SubTask 11.7: 验证线程安全：无竞争、无死锁

# Task Dependencies
- Task 1 独立（核心bug修复，最高优先级，立即生效）
- Task 2 独立（结构体增强）
- Task 3 依赖 Task 2
- Task 4 依赖 Task 3
- Task 5 依赖 Task 3
- Task 6 依赖 Task 4 和 Task 5
- Task 7 依赖 Task 6
- Task 8 依赖 Task 7
- Task 9 依赖 Task 8（且保留Task 1作为fallback）
- Task 10 依赖 Task 8
- Task 11 依赖所有前置任务

# 并行执行建议
- Task 1 独立优先执行（最小修复，立即解决用户问题）
- Task 2 独立（结构体修改）
- Task 4 + Task 5 可并行（都依赖Task 3，但函数独立）
- Task 9 + Task 10 可并行（都依赖Task 8）
