# Tasks

- [ ] Task 1: 修复network_cmd_put使用write_all
  - [ ] SubTask 1.1: `client/network_task.c` 第1598行，`write(g_sockfd, pkt, (size_t)len)` 改为 `write_all(g_sockfd, pkt, (size_t)len)`
  - [ ] SubTask 1.2: 检查`write_all`返回值，如果<0则`free(pkt); return false`

- [ ] Task 2: 修复network_cmd_put状态设置顺序
  - [ ] SubTask 2.1: `client/network_task.c` 第1578-1582行，将`g_ul_filename`、`g_ul_local_path`、`g_state = ST_WAIT_PUT_RESP`的设置移到第1593行（重复检查通过之后）、第1595行（发送PUT命令之前）
  - [ ] SubTask 2.2: 确保重复检查失败时`g_state`保持`ST_IDLE`

- [ ] Task 3: 修复服务端send_packet使用write_all
  - [ ] SubTask 3.1: `server/src/protocol.c` 第93行，`write(fd, pkt, (size_t)pkg_len)` 改为循环写入直到所有字节完成
  - [ ] SubTask 3.2: 在`protocol.c`中新增`write_all`静态函数（或复用已有）
  - [ ] SubTask 3.3: 检查返回值，部分写时循环写入剩余字节

- [ ] Task 4: 添加start_upload调试日志
  - [ ] SubTask 4.1: `client/network_task.c` `start_upload`函数中，`lseek`后添加`printf`打印`local_path`、`g_ul_fd`、`g_ul_total`
  - [ ] SubTask 4.2: 如果`g_ul_total <= 0`，打印警告日志

- [ ] Task 5: 添加handle_upload_chunk调试日志
  - [ ] SubTask 5.1: `client/network_task.c` `handle_upload_chunk`函数中，每次`read`和`write_all`后打印`r`、`w`、`g_ul_sent`、`g_ul_total`

- [ ] Task 6: 验证和测试
  - [ ] SubTask 6.1: 编译客户端无错误
  - [ ] SubTask 6.2: 编译服务端无错误
  - [ ] SubTask 6.3: 上传.c文件，检查调试日志确认`g_ul_total`正确
  - [ ] SubTask 6.4: 上传.c文件，服务器端文件内容与客户端完全一致
  - [ ] SubTask 6.5: 上传大文件（超过TCP发送缓冲区），内容完整
  - [ ] SubTask 6.6: 测试重复文件检查失败后状态机正常

# Task Dependencies
- Task 1 独立
- Task 2 独立
- Task 3 独立
- Task 4 独立
- Task 5 独立
- Task 6 依赖所有前置任务

# 并行执行建议
- Task 1 + Task 2 + Task 3 + Task 4 + Task 5 全部可并行（修改不同位置）
