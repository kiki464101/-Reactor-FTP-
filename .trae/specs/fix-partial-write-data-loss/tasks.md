# Tasks

- [ ] Task 1: 新增write_all辅助函数
  - [ ] SubTask 1.1: `client/network_task.c` 新增`write_all(int fd, const void *buf, size_t n)`函数：
    - 循环写入直到所有n字节完成
    - 处理EINTR（continue重试）
    - 处理EAGAIN/EWOULDBLOCK（对阻塞socket不应发生，但usleep后重试）
    - 处理真正错误（返回-1）
    - 返回成功写入的总字节数（应等于n）或-1
  - [ ] SubTask 1.2: `server/src/handler.c` 新增相同的`write_all`静态函数

- [ ] Task 2: 修复客户端handle_upload_chunk
  - [ ] SubTask 2.1: `client/network_task.c` 第1037行，将`write(g_sockfd, buf, r)`替换为`write_all(g_sockfd, buf, r)`
  - [ ] SubTask 2.2: 检查返回值：`write_all`返回-1时`return -1`，返回n时`g_ul_sent += n`
  - [ ] SubTask 2.3: 验证上传文件内容完整

- [ ] Task 3: 修复客户端handle_download_chunk
  - [ ] SubTask 3.1: `client/network_task.c` 第1009行，将`write(g_dl_fd, buf, r)`替换为`write_all(g_dl_fd, buf, r)`
  - [ ] SubTask 3.2: 检查返回值：`write_all`返回-1时`return -1`，返回n时`g_dl_received += n`
  - [ ] SubTask 3.3: 验证下载文件内容完整

- [ ] Task 4: 修复服务端worker_handle_get
  - [ ] SubTask 4.1: `server/src/handler.c` 第432行，将`write(sess->fd, buf, r)`替换为`write_all(sess->fd, buf, r)`
  - [ ] SubTask 4.2: 检查返回值：`write_all`返回-1时break，返回n时`sent += n`
  - [ ] SubTask 4.3: 验证服务端发送的文件内容完整

- [ ] Task 5: 修复服务端worker_handle_put
  - [ ] SubTask 5.1: `server/src/handler.c` 第514行，将`write(fd, buf, r)`替换为`write_all(fd, buf, r)`
  - [ ] SubTask 5.2: 检查返回值：`write_all`返回-1时break，返回n时`received += n`
  - [ ] SubTask 5.3: 验证服务端接收的文件内容完整

- [ ] Task 6: 验证和测试
  - [ ] SubTask 6.1: 编译客户端无错误
  - [ ] SubTask 6.2: 编译服务端无错误
  - [ ] SubTask 6.3: 测试上传文件，服务器端文件内容与客户端完全一致
  - [ ] SubTask 6.4: 测试下载文件，本地文件内容与服务器端完全一致
  - [ ] SubTask 6.5: 测试大文件传输（超过TCP发送缓冲区大小），内容完整
  - [ ] SubTask 6.6: 测试文件夹传输，所有文件内容完整

# Task Dependencies
- Task 1 独立（辅助函数）
- Task 2 依赖 Task 1（需要write_all）
- Task 3 依赖 Task 1
- Task 4 依赖 Task 1
- Task 5 依赖 Task 1
- Task 6 依赖 Task 2, 3, 4, 5

# 并行执行建议
- Task 1 优先执行
- Task 2 + Task 3 可并行（客户端两个函数独立）
- Task 4 + Task 5 可并行（服务端两个函数独立）
