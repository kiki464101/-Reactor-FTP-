# Checklist

## 状态机竞态条件修复（核心bug）
- [ ] `ST_WAIT_GET_RESP` else分支检查`FTP_CMD_LS`响应，交给LS处理逻辑
- [ ] `ST_WAIT_GET_RESP` else分支检查`FTP_CMD_DONE`，忽略
- [ ] `ST_WAIT_PUT_RESP` else分支检查`FTP_CMD_LS`和`FTP_CMD_DONE`
- [ ] `ST_WAIT_LISTDIR_RESP` else分支检查`FTP_CMD_LS`和`FTP_CMD_DONE`
- [ ] LS响应在传输等待状态下不被误判为失败
- [ ] `g_batch_done`不被LS响应错误递增
- [ ] 提取`handle_ls_response`函数避免代码重复
- [ ] 下载过程中点击文件夹导航，子文件夹内容正常显示

## transfer_worker_t结构体增强
- [ ] 新增`bool busy`字段
- [ ] 新增`int sockfd`字段
- [ ] 新增`int progress_percent`字段
- [ ] 新增`char current_filename[256]`字段
- [ ] 新增`bool is_upload`字段
- [ ] 新增`pthread_mutex_t mutex`字段
- [ ] `g_tx_workers`数组初始化（mutex init）

## Worker线程独立连接和登录
- [ ] `worker_connect_and_login`函数实现
- [ ] 创建socket成功
- [ ] 连接服务器成功
- [ ] 发送LOGIN命令并收到成功响应
- [ ] 连接/登录失败时返回false并清理

## Worker下载函数
- [ ] `worker_do_download`函数实现
- [ ] 用`w->sockfd`发送GET命令
- [ ] 用`w->sockfd`等待GET响应并解析filesize
- [ ] `mkdir_p`创建本地子目录
- [ ] `open`创建本地文件
- [ ] 循环read/write传输数据
- [ ] `usleep`保证进度条可见
- [ ] `lv_async_call`更新进度
- [ ] 检查`g_transfer_cancelled`标志
- [ ] 完成后消耗`FTP_CMD_DONE`包
- [ ] 关闭文件描述符

## Worker上传函数
- [ ] `worker_do_upload`函数实现
- [ ] `normalize_local_path`构建路径
- [ ] `stat`获取文件大小
- [ ] `open`本地文件
- [ ] 用`w->sockfd`发送PUT命令（含filesize）
- [ ] 用`w->sockfd`等待PUT ACK
- [ ] 循环read/write传输数据
- [ ] `lv_async_call`更新进度
- [ ] 检查`g_transfer_cancelled`标志
- [ ] 完成后消耗`FTP_CMD_DONE`包
- [ ] 关闭文件描述符

## Worker线程主循环
- [ ] `transfer_worker_thread_func`函数实现
- [ ] `tx_queue_pop`阻塞等待任务
- [ ] 检查`g_network_running`退出标志
- [ ] `w->sockfd < 0`时调用`worker_connect_and_login`
- [ ] 设置`w->busy`和`w->current_filename`
- [ ] 调用`worker_do_download`或`worker_do_upload`
- [ ] `g_batch_done++`（mutex保护）
- [ ] batch完成检查和通知
- [ ] 退出时关闭socket

## 线程池生命周期管理
- [ ] `transfer_pool_init`创建worker线程
- [ ] `transfer_pool_stop`停止worker线程
- [ ] `tx_queue_cancel_all` + `pthread_cond_broadcast`唤醒等待的worker
- [ ] `pthread_join`等待所有worker退出
- [ ] 登录成功后调用`transfer_pool_init`
- [ ] 网络线程退出前调用`transfer_pool_stop`

## network_cmd_get_multi/put_multi修改
- [ ] `network_cmd_get_multi`：LISTDIR请求+解析+入队
- [ ] `network_cmd_put_multi`：collect_files_recursive+入队
- [ ] `pthread_cond_broadcast`通知worker
- [ ] 移除`batch_start_next`串行调度

## 主网络线程简化
- [ ] 不再处理ST_DOWNLOADING/ST_UPLOADING
- [ ] 只处理LOGIN、LS、LISTDIR
- [ ] 保留Task 1的状态机修复作为fallback

## UI进度条多任务支持
- [ ] `ui_show_progress_batch`为每个worker创建独立进度条
- [ ] `cb_worker_progress`回调更新指定worker进度
- [ ] 进度更新携带worker ID

## 线程安全验证
- [ ] 传输队列mutex+cond保护push/pop
- [ ] 进度更新通过lv_async_call到UI线程
- [ ] 取消标志volatile bool跨线程可见
- [ ] 每个worker独立socket无竞争
- [ ] g_batch_done/g_batch_total通过mutex保护
- [ ] 无死锁（锁顺序一致）

## 编译和功能验证
- [ ] 服务端编译通过无错误
- [ ] 客户端编译通过无错误
- [ ] 下载包含多个文件的文件夹成功
- [ ] 多级子文件夹导航正常（可打开任意层级）
- [ ] 下载过程中点击文件夹导航正常显示
- [ ] 并发下载进度条独立显示
- [ ] 取消传输所有worker停止
