# Checklist

## write_all辅助函数
- [ ] `client/network_task.c` 新增`write_all`函数
- [ ] `write_all`循环写入直到所有字节完成
- [ ] `write_all`处理EINTR（重试）
- [ ] `write_all`处理EAGAIN/EWOULDBLOCK（usleep后重试）
- [ ] `write_all`处理真正错误（返回-1）
- [ ] `server/src/handler.c` 新增相同的`write_all`函数

## 客户端handle_upload_chunk修复
- [ ] `write(g_sockfd, buf, r)`替换为`write_all(g_sockfd, buf, r)`
- [ ] `write_all`返回-1时return -1
- [ ] `write_all`返回n时`g_ul_sent += n`
- [ ] 上传文件内容完整

## 客户端handle_download_chunk修复
- [ ] `write(g_dl_fd, buf, r)`替换为`write_all(g_dl_fd, buf, r)`
- [ ] `write_all`返回-1时return -1
- [ ] `write_all`返回n时`g_dl_received += n`
- [ ] 下载文件内容完整

## 服务端worker_handle_get修复
- [ ] `write(sess->fd, buf, r)`替换为`write_all(sess->fd, buf, r)`
- [ ] `write_all`返回-1时break
- [ ] `write_all`返回n时`sent += n`
- [ ] 服务端发送的文件内容完整

## 服务端worker_handle_put修复
- [ ] `write(fd, buf, r)`替换为`write_all(fd, buf, r)`
- [ ] `write_all`返回-1时break
- [ ] `write_all`返回n时`received += n`
- [ ] 服务端接收的文件内容完整

## 编译和功能验证
- [ ] 客户端编译通过无错误
- [ ] 服务端编译通过无错误
- [ ] 上传文件内容与原文件完全一致
- [ ] 下载文件内容与原文件完全一致
- [ ] 大文件传输内容完整
- [ ] 文件夹传输所有文件内容完整
