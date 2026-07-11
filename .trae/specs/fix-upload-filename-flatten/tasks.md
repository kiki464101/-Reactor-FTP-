# Tasks

修改集中在 `client/network_task.c` 一个文件中。

- [ ] Task 1: 在 `network_cmd_put()` 中提取 basename 发送给服务器
  - [ ] 1.1 在 `network_cmd_put()`（第 1048-1073 行）中，在 `build_cmd_put` 调用前，从 `filename` 中提取 basename：
    ```
    /* 发送给服务器的文件名仅取 basename，避免服务器创建子目录 */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    ```
  - [ ] 1.2 将 `build_cmd_put(filename, filesize, &len)` 改为 `build_cmd_put(base, filesize, &len)`
  - [ ] 1.3 `g_ul_filename` 仍存完整 `filename`（本地读取 `start_upload` 需要）
  - 修改后完整函数逻辑：
    ```
    bool network_cmd_put(const char *filename)
    {
        if (!g_network_running || g_sockfd < 0 || !filename) return false;

        /* check local file exists under ./client/ */
        struct stat st;
        char path[520];
        normalize_local_path(filename, path, sizeof(path));
        if (stat(path, &st) != 0) return false;
        if (!S_ISREG(st.st_mode)) return false;
        int filesize = (int)st.st_size;

        g_ul_filename[0] = '\0';
        strncpy(g_ul_filename, filename, sizeof(g_ul_filename) - 1);
        g_state = ST_WAIT_PUT_RESP;

        /* 发送给服务器的文件名仅取 basename，避免服务器创建子目录 */
        const char *base = strrchr(filename, '/');
        base = base ? base + 1 : filename;

        int len;
        unsigned char *pkt = build_cmd_put(base, filesize, &len);
        if (!pkt) return false;
        write(g_sockfd, pkt, (size_t)len);
        free(pkt);
        return true;
    }
    ```

- [ ] Task 2: 编译验证
  - [ ] 2.1 代码审查：确认 `strrchr` 所需 `<string.h>` 已包含（通常已存在）
  - [ ] 2.2 用户在 Linux 虚拟机中编译验证

# Task Dependencies
- Task 2 依赖 Task 1 完成

# 关键设计决策
1. **仅改客户端**：服务器逻辑正确——它按收到的文件名存储。问题是客户端发送了带子目录前缀的文件名。
2. **basename 提取**：`strrchr(filename, '/')` 找最后一个 `/`，取其后部分。无 `/` 时直接用原文件名。
3. **`g_ul_filename` 保持完整路径**：`start_upload()` 中 `normalize_local_path(g_ul_filename, ...)` 需要完整路径来打开本地文件。
4. **服务器 mkdir_p 可保留**：虽然现在客户端只发 basename，服务器端的 `mkdir_p` 逻辑不会触发（basename 不含 `/`），但保留无害。
5. **同目录同名文件覆盖**：如果 `./copy/` 下已存在同名文件，服务器会 `O_TRUNC` 覆盖，这是 FTP 的正常行为。
