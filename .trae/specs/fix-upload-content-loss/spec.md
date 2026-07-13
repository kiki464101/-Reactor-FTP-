# 修复上传文件内容丢失 Spec

## Why
用户反馈上传.c文件时文件内容未传输。终端日志显示`finish_upload: network_task.c success=1`立即完成，没有经历分块传输过程。经代码调查确认存在3个bug：(1)`network_cmd_put`第1598行使用`write`而非`write_all`，PUT命令包可能部分写导致服务器解析错误;(2)`g_state = ST_WAIT_PUT_RESP`在重复检查之前设置，若检查失败返回false但状态已被污染;(3)服务端`send_packet`使用`write`而非`write_all`，ACK/DONE包可能部分写。

## What Changes
- **修复`network_cmd_put`第1598行**：`write` → `write_all`，确保PUT命令包完整发送
- **修复`network_cmd_put`状态设置顺序**：将`g_state = ST_WAIT_PUT_RESP`移到所有检查之后
- **修复服务端`send_packet`**：`write` → `write_all`，确保ACK/DONE包完整发送
- **添加调试日志**：在`start_upload`打印`g_ul_total`和`local_path`，在`handle_upload_chunk`打印字节数

## Impact
- Affected specs: fix-partial-write-data-loss, fix-upload-include-parent-dir
- Affected code:
  - `client/network_task.c`: `network_cmd_put`第1582-1598行、`start_upload`第899-928行、`handle_upload_chunk`第1044-1063行
  - `server/src/protocol.c`: `send_packet`第93行

## 问题根本原因分析

### Bug 1: network_cmd_put使用write而非write_all（第1598行）

```c
write(g_sockfd, pkt, (size_t)len);  // BUG: 可能部分写
```

`write`在socket上可能返回小于`len`的值（TCP发送缓冲区满时）。剩余字节被丢弃，服务器收到不完整的PUT命令。服务器`read_packet`读取到错误的`pkg_len`或`filesize`，可能导致：
- 服务器解析`filesize=0`，创建空文件，发送ACK
- 客户端收到ACK，`start_upload`获取正确的`g_ul_total`，但服务器只期望0字节
- 客户端发送数据，服务器不接收（已发DONE），客户端`finish_upload`读到DONE报告成功
- **结果：服务器文件为空，客户端报告成功**

### Bug 2: g_state设置顺序错误（第1582行）

```c
g_state = ST_WAIT_PUT_RESP;  // 第1582行：在重复检查之前设置

if (ui_remote_list_has_entry(base)) {  // 第1589行
    return false;  // 返回false，但g_state已被污染！
}
```

如果重复检查失败，`network_cmd_put`返回false，但`g_state`已被设为`ST_WAIT_PUT_RESP`。`batch_start_next`看到`!ok`，跳过该任务，但状态机停留在`ST_WAIT_PUT_RESP`，下一个任务的命令响应会被误判。

### Bug 3: 服务端send_packet使用write而非write_all（protocol.c第93行）

```c
int ret = (int)write(fd, pkt, (size_t)pkg_len);  // BUG: 可能部分写
```

ACK/DONE包可能部分写，客户端收到不完整的响应包，`read_packet`解析失败或阻塞。

## ADDED Requirements

### Requirement: network_cmd_put使用write_all
`network_cmd_put` SHALL 使用`write_all`发送PUT命令包，确保所有字节完整写入socket。

#### Scenario: PUT命令完整发送
- **WHEN** 客户端发送PUT命令
- **THEN** 使用`write_all`循环写入直到所有字节完成
- **AND** 服务器收到完整的PUT命令包
- **AND** 服务器正确解析filename和filesize

### Requirement: network_cmd_put状态设置顺序
`network_cmd_put` SHALL 在所有检查通过后、发送PUT命令前才设置`g_state = ST_WAIT_PUT_RESP`。

#### Scenario: 重复文件检查失败
- **WHEN** 重复文件检查失败
- **THEN** `g_state`保持`ST_IDLE`
- **AND** `network_cmd_put`返回false
- **AND** 状态机不被污染

### Requirement: 服务端send_packet使用write_all
`send_packet` SHALL 使用`write_all`发送响应包，确保ACK/DONE包完整发送。

### Requirement: 上传调试日志
`start_upload`和`handle_upload_chunk` SHALL 打印调试日志，便于诊断内容传输问题。

## MODIFIED Requirements

### Requirement: network_cmd_put函数
`network_cmd_put` SHALL：
1. 检查文件存在性和类型
2. 检查重复文件
3. 通过所有检查后，设置`g_ul_filename`、`g_ul_local_path`、`g_state = ST_WAIT_PUT_RESP`
4. 使用`write_all`发送PUT命令包

**Previous**: `g_state`在重复检查之前设置，`write`可能部分写
**New**: `g_state`在所有检查之后设置，`write_all`确保完整写入
