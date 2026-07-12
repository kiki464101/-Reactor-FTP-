# 修复多级子文件夹导航和并发传输 Spec

## Why
用户反馈"只能打开一层文件夹，无法打开两层文件夹（父文件夹和子文件夹），而且下载文件夹还是只能下载文件夹里的一个文件"。

经深入代码调查确认，**根本原因是状态机竞态条件**：当`g_state == ST_WAIT_GET_RESP`（下载等待中）时，如果用户点击文件夹导航触发`network_cmd_ls`，LS响应到达后会被状态机误判为GET失败（走else分支显示"Server: file not found"），LS响应被丢弃，文件夹内容不显示。

同时，下载/上传在主socket上进行，与LS命令共用同一个状态机，导致传输和导航互相干扰。用户要求用多线程并发+线程池+epoll解决此类问题。

## What Changes
- **修复状态机竞态条件**：在`ST_WAIT_GET_RESP`/`ST_WAIT_PUT_RESP`/`ST_WAIT_LISTDIR_RESP`的else分支中，区分响应类型——如果是`FTP_CMD_LS`响应，交给LS处理逻辑而不是误判为失败
- **实现传输线程池**：创建多个worker线程，每个用独立socket并发下载/上传，用mutex+cond保证线程安全，主socket专用于命令（LS/LISTDIR），彻底隔离传输和导航
- **进度条支持多任务**：每个worker独立进度，UI显示多个进度条

## Impact
- Affected specs: fix-folder-download-only-one-file, add-long-press-folder-transfer, add-transfer-throttle
- Affected code:
  - `client/network_task.c`: 状态机else分支修复、新增`transfer_worker_thread_func`、新增`worker_do_download`/`worker_do_upload`、修改`network_cmd_get_multi`/`network_cmd_put_multi`
  - `client/network_task.h`: `transfer_worker_t`结构体增强
  - `client/ui_manager.c`: 进度条多任务显示

## 问题根本原因分析（已通过代码调查确认）

### 根本原因：状态机竞态条件（client/network_task.c 第1016-1028行）

```c
if (g_state == ST_WAIT_GET_RESP) {
    if (rsp2.cmd_no == FTP_CMD_GET && rsp2.res_result == 1) {
        // 正常下载
    } else {
        // BUG: 所有非GET响应都被误判为失败！
        str_data_t *err = make_str_data("Server: file not found");
        if (err) lv_async_call(cb_show_error_popup, err);
        g_state = ST_IDLE;
        if (g_batch_active) { g_batch_done++; }
    }
    free(payload);
    continue;  // LS响应被丢弃！
}
```

**触发场景**：
1. 用户下载文件夹（触发LISTDIR + 多个GET任务，g_state = ST_WAIT_GET_RESP）
2. 下载过程中，用户点击子文件夹导航
3. `network_cmd_ls(path)`发送LS命令到主socket
4. 服务器先处理LS，返回LS响应
5. 客户端主线程poll到LS响应，但`g_state == ST_WAIT_GET_RESP`
6. `rsp2.cmd_no == FTP_CMD_LS != FTP_CMD_GET`，进入else分支
7. 显示"Server: file not found"错误
8. LS响应被丢弃，`g_batch_done++`错误跳过文件
9. **子文件夹内容不显示** → 用户看到"只能打开一层文件夹"

**同样影响ST_WAIT_PUT_RESP**（第1030-1047行）和**ST_WAIT_LISTDIR_RESP**（第1049-1086行）。

### 已确认修复的项（无需重复修改）
- FTP_CMD_DONE包消耗：`finish_download`第683-688行、`finish_upload`第783-788行 ✓
- g_long_pressed取消选中设置：`on_remote_file_item_long_pressed`第1426行、`on_local_file_item_long_pressed`第1369行 ✓
- 多级子目录路径拼接：`on_file_item_clicked`第735-751行（前进）、第726-732行（后退）✓
- 服务端LS支持多级路径：`worker_handle_ls`第210行 ✓
- 服务端LISTDIR递归：`listdir_recursive`第251-302行 ✓

## ADDED Requirements

### Requirement: 状态机区分响应类型（最小修复）
状态机 SHALL 在`ST_WAIT_GET_RESP`/`ST_WAIT_PUT_RESP`/`ST_WAIT_LISTDIR_RESP`的else分支中，检查响应类型：
- 如果是`FTP_CMD_LS`响应，交给LS处理逻辑（`case FTP_CMD_LS`）
- 如果是`FTP_CMD_DONE`，忽略
- 只有真正是对应命令的失败响应才报错

#### Scenario: 下载过程中点击文件夹导航
- **WHEN** 下载进行中（g_state == ST_WAIT_GET_RESP）用户点击子文件夹
- **THEN** LS响应被正确识别并交给LS处理逻辑
- **AND** 子文件夹内容正常显示
- **AND** 不显示"Server: file not found"错误

### Requirement: 传输线程池并发下载（彻底隔离）
系统 SHALL 使用线程池管理多个传输worker线程，每个worker用独立socket连接服务器，并发下载/上传。主socket专用于命令（LS/LISTDIR），彻底隔离传输和导航。

#### Scenario: 并发下载文件夹
- **WHEN** 用户长按选中hahahah/文件夹下载（包含3个文件）
- **THEN** 3个文件被分配到线程池的worker线程
- **AND** 多个文件同时下载（受TRANSFER_POOL_SIZE限制）
- **AND** 每个文件的进度独立显示
- **AND** 下载过程中用户可以正常导航文件夹（主socket不受影响）

#### Scenario: 线程安全保证
- **WHEN** 多个worker线程同时运行
- **THEN** 传输队列通过mutex+cond保护push/pop
- **AND** 进度更新通过lv_async_call到UI线程
- **AND** 取消标志为volatile bool，跨线程可见
- **AND** 每个worker的socket独立，无竞争

## MODIFIED Requirements

### Requirement: transfer_worker_t结构体增强
`transfer_worker_t` SHALL 包含worker线程的完整状态：
- `pthread_t thread`：线程ID
- `int id`：worker编号
- `bool running`：是否运行中
- `int sockfd`：独立socket
- `bool busy`：是否正在传输
- `int progress_percent`：当前进度
- `char current_filename[256]`：当前传输的文件名
- `bool is_upload`：是否上传
- `pthread_mutex_t mutex`：保护本worker状态

## 解决方案

### 方案A：最小修复状态机（立即生效）
在else分支中检查响应类型，如果是LS响应则转交处理：
```c
if (g_state == ST_WAIT_GET_RESP) {
    if (rsp2.cmd_no == FTP_CMD_GET && rsp2.res_result == 1) {
        start_download(g_dl_filename, filesize);
    } else if (rsp2.cmd_no == FTP_CMD_LS) {
        /* LS响应在下载等待期间到达，交给LS处理 */
        if (rsp2.res_result == 1) {
            // 处理LS响应...
        }
        g_state = ST_IDLE;  // 不递增g_batch_done
    } else {
        // 真正的GET失败
        show_error("Server: file not found");
        g_state = ST_IDLE;
        if (g_batch_active) { g_batch_done++; }
    }
}
```

### 方案B：线程池彻底隔离（用户要求的并发方案）

#### 架构设计
```
UI线程                    主网络线程              Worker线程池
  |                         |                      |
  | network_cmd_get_multi   |                      |
  |------------------------>|                      |
  |                         | tx_queue_push        |
  |                         |---------------------->|
  |                         |    (cond_signal)     |
  |                         |                      | pop任务
  | network_cmd_ls          |                      | connect+login(独立socket)
  |------------------------>|                      | GET请求(独立socket)
  |                         | (主socket只处理LS)   | 下载数据
  |                         |                      | lv_async_call(进度)
  |                         |                      | 完成→取下一个
```

#### Worker线程流程
1. 创建独立socket，连接服务器，登录
2. `tx_queue_pop`取任务（mutex+cond阻塞等待）
3. 发送GET/PUT命令（用worker自己的socket）
4. 传输文件数据（read/write循环）
5. 通过`lv_async_call`更新进度到UI
6. 检查`g_transfer_cancelled`标志
7. 消耗`FTP_CMD_DONE`包
8. 完成后回到步骤2

#### 线程安全保证
- **队列**：`pthread_mutex_t` + `pthread_cond_t`保护push/pop
- **进度**：每个worker独立进度，通过`lv_async_call`到UI线程
- **取消**：`volatile bool g_transfer_cancelled`，worker定期检查
- **socket**：每个worker独立socket，主socket专用于LS/LISTDIR，无竞争
- **登录凭据**：`g_login_ip/port/user/pass`只读，worker线程安全读取

#### 关键函数
- `transfer_pool_init()`：创建TRANSFER_POOL_SIZE个worker线程
- `transfer_pool_stop()`：设置停止标志，join所有worker
- `transfer_worker_thread_func()`：worker线程主循环
- `worker_connect_and_login()`：worker独立连接+登录
- `worker_do_download()`：worker下载单个文件（独立socket）
- `worker_do_upload()`：worker上传单个文件（独立socket）
