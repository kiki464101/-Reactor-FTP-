# Tasks

修改集中在 `client/network_task.c` 一个文件中。

- [x] Task 1: 添加重复文件检测辅助函数
  - [x] 1.1 在 `network_task.c` 中添加静态辅助函数 `check_file_exists`，用于检查指定目录下是否存在同名文件：
    ```
    /* 检查指定目录下是否存在同名文件 */
    static bool check_file_exists(const char *dir, const char *filename)
    {
        char path[520];
        snprintf(path, sizeof(path), "%s/%s", dir, filename);
        struct stat st;
        return (stat(path, &st) == 0);
    }
    ```
  - [x] 1.2 确认 `#include <sys/stat.h>` 已包含（通常已存在）

- [x] Task 2: 在 `network_cmd_put()` 中增加上传重复检测
  - [x] 2.1 在 `network_cmd_put()` 中，在提取 basename 之后、调用 `build_cmd_put` 之前，增加服务器端重复检测：
    ```
    /* 检查服务器端是否已存在同名文件 */
    if (check_file_exists("./copy", base)) {
        str_data_t *err = make_str_data("file have exist");
        if (err) lv_async_call(cb_show_error_popup, err);
        return false;
    }
    ```

- [x] Task 3: 在 `network_cmd_put_multi()` 中增加上传重复检测
  - [x] 3.1 在 `network_cmd_put_multi()` 的验证循环中，在 `tx_queue_push` 之前增加 basename 提取和重复检测：
    ```
    /* 提取 basename */
    const char *base = strrchr(filenames[i], '/');
    base = base ? base + 1 : filenames[i];

    /* 检查服务器端是否已存在同名文件 */
    if (check_file_exists("./copy", base)) {
        str_data_t *err = make_str_data("file have exist");
        if (err) lv_async_call(cb_show_error_popup, err);
        continue;
    }
    ```
  - 注意：用 `continue` 跳过重复文件，继续处理队列中其它文件

- [x] Task 4: 在 `start_download()` 中增加下载重复检测
  - [x] 4.1 在 `start_download()` 中，在 `open()` 调用之前增加本地重复检测：
    ```
    /* 检查本地是否已存在同名文件 */
    if (check_file_exists("./client/load", filename)) {
        str_data_t *err = make_str_data("file have exist");
        if (err) lv_async_call(cb_show_error_popup, err);
        g_state = ST_IDLE;
        return;
    }
    ```

- [x] Task 5: 编译验证
  - [x] 5.1 代码审查：确认所有修改正确
  - [ ] 5.2 用户在 Linux 虚拟机中编译验证

# Task Dependencies
- Task 2、Task 3 依赖 Task 1（需要 `check_file_exists` 函数）
- Task 4 依赖 Task 1（需要 `check_file_exists` 函数）
- Task 2 和 Task 3 可并行修改
- Task 5 依赖 Task 1-4 完成

# 关键设计决策
1. **检测方式**：使用 `stat()` 检查文件是否存在，简单可靠，无需修改服务端协议。
2. **上传检测在客户端做**：客户端直接 `stat("./copy/...")` 检查服务器共享目录。因为客户端和服务器在同一台机器上运行（开发环境），路径 `./copy/` 可直接访问。如果将来需要跨机器部署，可改为 LS 列表对比，但当前方案最简单。
3. **下载检测在 `start_download` 中做**：在网络线程中执行，检测到重复时用 `lv_async_call` 弹窗（UI 操作必须在主线程），设置 `g_state = ST_IDLE` 让状态机回到空闲。
4. **弹窗复用 `ui_show_error_popup`**：已有 Close 按钮和单例保护，直接调用即可。
5. **多文件上传用 `continue` 跳过重复**：不中断整个批次，只跳过重复文件，继续处理队列中其它文件。
6. **不覆盖原文件**：检测到重复直接中止，不执行传输，保护已有文件不被覆盖。
