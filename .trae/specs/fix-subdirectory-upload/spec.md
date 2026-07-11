# Subdirectory Upload Fix Spec

## Why
上传子目录（如 `./client/test_delete/`）中的文件时，客户端发送的文件名包含子目录前缀（如 `test_delete/haha.txt`），服务器尝试在 `./copy/test_delete/haha.txt` 创建文件，但 `./copy/test_delete/` 目录不存在，导致 `open()` 失败，服务器返回拒绝，客户端显示 "Server rejected upload"。

## What Changes
- 服务器端 `worker_handle_put()` 在 `open()` 之前，先递归创建文件所在的父目录
- 客户端 `network_cmd_put()` 和 `network_cmd_put_multi()` 保持不变（文件名仍带子目录前缀）

## Impact
- Affected code:
  - `server/src/handler.c` — `worker_handle_put()` 增加父目录创建逻辑
- 不需要修改的文件：
  - `client/network_task.c` — 文件名传递逻辑不变
  - `client/ui_manager.c` — 选中逻辑不变

## ADDED Requirements

### Requirement: Create Parent Directory Before Upload
系统 SHALL 在服务器端创建上传文件之前，先递归创建文件所在的父目录。

#### Scenario: Upload File in Subdirectory
- **WHEN** 客户端上传文件 `test_delete/haha.txt`
- **THEN** 服务器检测到路径 `./copy/test_delete/haha.txt` 包含子目录
- **AND** 服务器先创建 `./copy/test_delete/` 目录（包括中间目录）
- **AND** 服务器在 `./copy/test_delete/haha.txt` 创建文件
- **AND** 上传成功完成

#### Scenario: Upload File in Root
- **WHEN** 客户端上传文件 `haha.txt`（无子目录前缀）
- **THEN** 服务器直接在 `./copy/haha.txt` 创建文件
- **AND** 不需要创建子目录
- **AND** 上传成功完成

#### Scenario: Upload File in Deep Subdirectory
- **WHEN** 客户端上传文件 `a/b/c.txt`（多级子目录）
- **THEN** 服务器递归创建 `./copy/a/` 和 `./copy/a/b/`
- **AND** 服务器在 `./copy/a/b/c.txt` 创建文件
- **AND** 上传成功完成

## MODIFIED Requirements

### Requirement: worker_handle_put
`worker_handle_put()` SHALL 在构造完整路径后、调用 `open()` 前，提取文件名的父目录部分，并使用 `mkdir_p()` 递归创建。

**Previous**:
```
char path[512];
snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);
int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
if (fd < 0) {
    tx(sess, FTP_CMD_PUT, 0, (unsigned char *)"cannot create", 13);
    return;
}
```

**New**:
```
char path[512];
snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);

/* create parent directories if filename contains '/' */
char *slash = strrchr(path, '/');
if (slash) {
    char dir[512];
    size_t dlen = (size_t)(slash - path);
    if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
    memcpy(dir, path, dlen);
    dir[dlen] = '\0';
    mkdir_p(dir);
}

int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
if (fd < 0) {
    tx(sess, FTP_CMD_PUT, 0, (unsigned char *)"cannot create", 13);
    return;
}
```

## REMOVED Requirements
无
