# Tasks

修改集中在 `client/ui_manager.c`、`client/ui_manager.h`、`client/network_task.c` 三个文件。

- [x] Task 1: 在 ui_manager 中新增展示文件名镜像数组
  - [x] 1.1 在 `ui_manager.c` 顶部（全局变量区，靠近 `g_selected_remote` 附近）新增：
    - `#define MAX_DISPLAYED_FILES 256`
    - `static char g_remote_displayed_files[MAX_DISPLAYED_FILES][256];`
    - `static int  g_remote_displayed_count = 0;`
    - `static char g_local_displayed_files[MAX_DISPLAYED_FILES][256];`
    - `static int  g_local_displayed_count = 0;`
  - [x] 1.2 在 `ui_update_file_list_cb()` 中，遍历 token 添加列表按钮的循环里，同步把每个有效文件名（跳过 "." 、 ".." ）追加到 `g_remote_displayed_files[]`，并更新 `g_remote_displayed_count`；在循环开始前先重置 count 为 0
  - [ ] 1.3 在 `ui_update_local_file_list_cb()` 中，遍历 token 添加列表按钮的循环里，同步把每个**文件**条目（跳过路径头文本、 ".." 、以 "/" 结尾的目录条目）追加到 `g_local_displayed_files[]`，并更新 `g_local_displayed_count`；在循环开始前先重置 count 为 0

- [x] Task 2: 新增公共查询函数
  - [x] 2.1 在 `ui_manager.c` 中实现：
    ```
    bool ui_remote_list_has_file(const char *name)
    {
        if (!name) return false;
        for (int i = 0; i < g_remote_displayed_count; i++) {
            if (strcmp(g_remote_displayed_files[i], name) == 0) return true;
        }
        return false;
    }

    bool ui_local_list_has_file(const char *name)
    {
        if (!name) return false;
        for (int i = 0; i < g_local_displayed_count; i++) {
            if (strcmp(g_local_displayed_files[i], name) == 0) return true;
        }
        return false;
    }
    ```
  - [x] 2.2 在 `ui_manager.h` 中声明这两个函数（放在已有 `ui_update_file_list_cb` 等声明附近）：
    ```
    bool ui_remote_list_has_file(const char *name);
    bool ui_local_list_has_file(const char *name);
    ```

- [x] Task 3: 修改 network_task.c 上传重复检测
  - [x] 3.1 在 `network_cmd_put()` 中，将 `if (check_file_exists("./copy", base))` 替换为 `if (ui_remote_list_has_file(base))`，弹窗文案从 `"file have exist"` 改为 `"repeat file"`
  - [x] 3.2 在 `network_cmd_put_multi()` 中，将 `if (check_file_exists("./copy", base))` 替换为 `if (ui_remote_list_has_file(base))`，弹窗文案从 `"file have exist"` 改为 `"repeat file"`，保留 `continue` 逻辑

- [x] Task 4: 修改 network_task.c 下载重复检测
  - [x] 4.1 在 `start_download()` 中，将 `if (check_file_exists("./client/load", filename))` 替换为 `if (ui_local_list_has_file(filename))`，弹窗文案从 `"file have exist"` 改为 `"repeat file"`

- [x] Task 5: 删除不再使用的 check_file_exists
  - [x] 5.1 删除 `network_task.c` 中两处 `check_file_exists` 定义（约第 73 行和第 333 行附近的 static 函数）

- [x] Task 6: 编译验证
  - [x] 6.1 代码审查：确认所有调用点已替换、文案统一为 "repeat file"、无遗留 `check_file_exists` 引用
  - [ ] 6.2 用户在 Linux 虚拟机中编译验证无错误无警告

# Task Dependencies
- Task 2 依赖 Task 1（查询函数读取镜像数组）
- Task 3、Task 4 依赖 Task 2（需要查询函数）
- Task 5 依赖 Task 3、Task 4（确认无调用后再删除）
- Task 6 依赖 Task 1-5 全部完成
- Task 1 的 1.2 和 1.3 可独立修改（分别针对远程/本地回调）

# 关键设计决策
1. **镜像数组而非遍历 LVGL 控件**：直接遍历 `lv_list` 子控件获取按钮文本较脆弱且依赖 LVGL 内部结构。改为在填充列表时同步维护一份纯 C 数组，查询简单、解耦 UI 框架。
2. **数组容量 256**：与 `MAX_SELECTED_FILES` 同量级，足够覆盖常规文件列表。
3. **不加锁**：遵循现有代码风格（`g_selected_remote` 等也无锁），单写多读，最坏读稍过时。
4. **basename 提取保留**：上传时本地路径可能含子目录（`test_delete/test2.txt`），仍需提取 basename 再与远程展示列表对比。
5. **文案统一**：三处检测点（单文件上传、多文件上传、下载）文案全部改为 "repeat file"。
