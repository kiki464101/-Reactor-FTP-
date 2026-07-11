# Tasks

修改集中在 `server/src/handler.c` 一个文件中。

- [ ] Task 1: 实现 `mkdir_p()` 递归创建目录辅助函数
  - [ ] 1.1 在 `handler.c` 文件顶部（`#include` 区域之后、函数定义之前）添加辅助函数：
    ```
    /* Recursively create directories like mkdir -p */
    static void mkdir_p(const char *path)
    {
        char tmp[512];
        strncpy(tmp, path, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        size_t len = strlen(tmp);
        if (len > 0 && tmp[len - 1] == '/')
            tmp[len - 1] = '\0';

        for (char *p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }
        mkdir(tmp, 0755);
    }
    ```
  - [ ] 1.2 确认 `#include <sys/stat.h>` 已包含（`mkdir` 需要），如果缺失则添加

- [ ] Task 2: 在 `worker_handle_put()` 中调用 `mkdir_p()` 创建父目录
  - [ ] 2.1 在 `worker_handle_put()`（第 273-276 行附近）修改：
    - **原代码**：
      ```
      char path[512];
      snprintf(path, sizeof(path), "%s/%s", MY_FTP_BOOT, filename);

      int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      ```
    - **改为**：
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
      ```
  - [ ] 2.2 确认 `#include <string.h>` 已包含（`strrchr`/`memcpy` 需要），通常已存在

- [ ] Task 3: 编译验证
  - [ ] 3.1 代码审查：确认所有修改正确
  - [ ] 3.2 用户在 Linux 虚拟机中编译验证

# Task Dependencies
- Task 2 依赖 Task 1（需要 `mkdir_p` 函数已定义）
- Task 3 依赖 Task 1 和 Task 2 完成

# 关键设计决策
1. **仅修改服务端**：客户端发送的文件名（如 `test_delete/haha.txt`）本身是正确的，问题出在服务端没有创建子目录。修改服务端是最小改动方案。
2. **`mkdir_p` 实现简单递归**：逐级创建路径中每个 `/` 分隔的目录，忽略 `mkdir` 的 EEXIST 错误（目录已存在时 `mkdir` 返回 -1，但无需处理）。
3. **`strrchr` 找最后一个 `/`**：从完整路径中提取父目录部分，例如 `./copy/test_delete/haha.txt` → 父目录 `./copy/test_delete`。
4. **根目录文件不受影响**：文件名不含 `/` 时，`path` 中的最后一个 `/` 在 `MY_FTP_BOOT`（`./copy`）中，`mkdir_p("./copy")` 只是确保 `./copy` 存在，无副作用。
