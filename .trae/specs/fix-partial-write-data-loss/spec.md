# 修复文件内容传输丢失 Spec

## Why
用户反馈"传输文件过来，但是文件的内容没有传输过来"。经代码调查确认，**根本原因是部分写（partial write）导致数据丢失**：在4个文件传输函数中，`read`读取N字节后`write`只写入M字节（M<N时），但只计数M字节，剩余N-M字节的文件内容被丢弃。

## What Changes
- **修复客户端`handle_upload_chunk`**：部分socket写导致上传文件内容丢失
- **修复客户端`handle_download_chunk`**：部分文件写导致下载文件内容丢失
- **修复服务端`worker_handle_get`**：部分socket写导致下载文件内容丢失
- **修复服务端`worker_handle_put`**：部分文件写导致上传文件内容丢失
- 新增`write_all`辅助函数确保所有字节都被写入

## Impact
- Affected specs: fix-folder-download-only-one-file, add-long-press-folder-transfer
- Affected code:
  - `client/network_task.c`: `handle_upload_chunk`、`handle_download_chunk`
  - `server/src/handler.c`: `worker_handle_get`、`worker_handle_put`

## 问题根本原因分析

### Bug模式：read N字节 → write M字节（M<N）→ 只计数M字节 → 丢失N-M字节

**4个位置都有这个bug**：

#### 1. 客户端`handle_upload_chunk`（network_task.c 第1028-1047行）—— 上传内容丢失
```c
int r = (int)read(g_ul_fd, buf, (size_t)to_read);   // 从文件读r字节，文件指针前进r
if (r <= 0) return -1;
int w = (int)write(g_sockfd, buf, (size_t)r);        // 只写入w字节（w可能<r）
g_ul_sent += w;                                      // 只计数w
// BUG: 字节w到r-1丢失！下次read从文件位置old+r读，但服务器期望old+w
```

#### 2. 客户端`handle_download_chunk`（network_task.c 第996-1015行）—— 下载内容丢失
```c
int r = (int)read(g_sockfd, buf, (size_t)to_read);   // 从socket读r字节
int w = (int)write(g_dl_fd, buf, (size_t)r);          // 只写入w字节到文件
g_dl_received += w;                                   // 只计数w
// BUG: 字节w到r-1丢失！socket已前进r，但文件只写了w
```

#### 3. 服务端`worker_handle_get`（handler.c 第428-434行）—— 下载内容丢失
```c
int r = (int)read(fd, buf, sizeof(buf));              // 从文件读r字节
int w = (int)write(sess->fd, buf, (size_t)r);         // 只写入w字节到socket
sent += w;                                            // 只计数w
// BUG: 同上
```

#### 4. 服务端`worker_handle_put`（handler.c 第506-516行）—— 上传内容丢失
```c
int r = (int)read(sess->fd, buf, (size_t)chunk);      // 从socket读r字节
int w = (int)write(fd, buf, (size_t)r);               // 只写入w字节到文件
received += w;                                        // 只计数w
// BUG: 同上
```

### 为什么socket写会部分写？
- TCP发送缓冲区满时，`write`只写入部分数据并返回实际写入字节数
- 文件较大或传输速度快时，发送缓冲区容易满
- 这是最常见的文件内容丢失原因

### 为什么文件写也会部分写？
- 磁盘满、文件系统限制等情况下`write`可能返回小于请求的字节数
- 虽然罕见，但正确代码应该处理这种情况

## ADDED Requirements

### Requirement: write_all辅助函数
系统 SHALL 提供`write_all`辅助函数，确保所有字节都被写入，处理部分写和EINTR。

#### Scenario: 部分写处理
- **WHEN** `write`返回小于请求的字节数
- **THEN** 循环写入剩余字节直到全部完成
- **AND** 正确处理EINTR（重试）

### Requirement: read_all辅助函数
系统 SHALL 提供`read_all`辅助函数（服务端），确保读取指定字节数，处理部分读和EINTR。

## MODIFIED Requirements

### Requirement: 客户端handle_upload_chunk完整写入
`handle_upload_chunk` SHALL 使用`write_all`将读取的数据全部写入socket，不丢失任何字节。

#### Scenario: 上传文件内容完整传输
- **WHEN** 用户上传一个文件
- **THEN** 文件的所有字节都被传输到服务器
- **AND** 服务器端文件内容与客户端文件内容完全一致

### Requirement: 客户端handle_download_chunk完整写入
`handle_download_chunk` SHALL 使用`write_all`将读取的数据全部写入本地文件，不丢失任何字节。

#### Scenario: 下载文件内容完整传输
- **WHEN** 用户下载一个文件
- **THEN** 文件的所有字节都被传输到本地
- **AND** 本地文件内容与服务器端文件内容完全一致

### Requirement: 服务端worker_handle_get完整写入
`worker_handle_get` SHALL 使用`write_all`将文件数据全部写入socket，不丢失任何字节。

### Requirement: 服务端worker_handle_put完整写入
`worker_handle_put` SHALL 使用`write_all`将接收的数据全部写入磁盘文件，不丢失任何字节。
