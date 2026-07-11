# Tasks

所有修改集中在 `client/ui_manager.c` 一个文件中。

- [x] Task 1: 添加前向声明和 Delete 按钮
  - [x] 1.1 在前向声明区（第 96 行 `on_disconnect_btn_clicked` 之后）添加：
    ```
    static void on_delete_btn_clicked(lv_event_t *e);
    ```
  - [x] 1.2 在 `ui_main_init()` 的按钮行（第 383 行 "Upload" 和第 384 行 "Disconnect" 之间）插入：
    ```
    create_btn(btn_row, "Delete",     90, 34, on_delete_btn_clicked);
    ```
  - 修改后按钮顺序为：Refresh | Download | Upload | Delete | Disconnect

- [x] Task 2: 实现 `on_delete_btn_clicked()` 回调函数
  - [x] 2.1 在 `on_upload_btn_clicked` 函数之后（约第 763 行之后）添加新函数，逻辑如下：
    ```
    static void on_delete_btn_clicked(lv_event_t *e)
    {
        (void)e;

        // 优先级 1：选中了远程文件 → 弹窗拦截
        if (g_remote_sel_count > 0) {
            ui_show_error_popup("error delete");
            return;
        }

        // 优先级 2：没有选中任何本地文件 → 状态栏提示
        if (g_local_sel_count == 0) {
            ui_show_error("No file selected");
            return;
        }

        // 优先级 3：执行本地文件删除
        int success = 0, fail = 0;
        for (int i = 0; i < g_local_sel_count; i++) {
            char *name = g_selected_local[i];
            size_t nlen = strlen(name);

            // 跳过目录条目（以 "/" 结尾）
            if (nlen > 0 && name[nlen - 1] == '/') {
                fail++;
                continue;
            }

            // 拼接完整路径，与 scan_local_directory 的路径逻辑保持一致
            char path[520];
            if (g_local_cur_path[0])
                snprintf(path, sizeof(path), "./client/%s/%s",
                         g_local_cur_path, name);
            else
                snprintf(path, sizeof(path), "./client/%s", name);

            // 执行删除
            if (remove(path) == 0)
                success++;
            else
                fail++;
        }

        // 清空选中状态（ui_refresh_local_files 回调也会清，但提前清避免竞态）
        g_local_sel_count = 0;
        memset(g_selected_local, 0, sizeof(g_selected_local));

        // 更新选中计数标签
        if (main_selected_label) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Remote: %d | Local: %d",
                     g_remote_sel_count, g_local_sel_count);
            lv_label_set_text(main_selected_label, buf);
        }

        // 刷新本地文件列表
        ui_refresh_local_files();

        // 显示结果
        if (fail == 0)
            ui_set_status("Deleted");
        else
            ui_show_error("Some files failed to delete");
    }
    ```
  - [x] 2.2 确认 `remove()` 所需头文件 `<stdio.h>` 已包含（第 13 行已有 `#include <stdio.h>`，`remove()` 声明在 `<stdio.h>` 中，无需额外 include）

- [x] Task 3: 验证 error popup 机制（无需改代码，仅确认）
  - [x] 3.1 确认 `ui_show_error_popup()` 已有单例保护：
    - 第 866 行 `if (err_popup && lv_obj_is_valid(err_popup))` 防止重复创建弹窗
    - 如果弹窗已存在，仅更新文本，不会出现弹窗叠加
  - [x] 3.2 确认 Close 按钮回调 `on_close_error_popup_btn_clicked`（第 851 行）：
    - 调用 `lv_obj_del(popup)` 销毁弹窗
    - 将 `err_popup` 和 `err_label` 置 NULL
    - 弹窗销毁后主循环继续正常运行，不阻塞任何操作
  - [x] 3.3 确认弹窗创建在 `lv_layer_top()` 上（通过 `ui_get_window_parent()`），不影响底层屏幕的交互

- [x] Task 4: 编译验证（代码审查通过，需用户在 Linux 虚拟机中编译）
  - [x] 4.1 代码审查验证：所有函数引用、变量引用、头文件包含均正确
  - [ ] 4.2 用户在 Linux 虚拟机中执行编译验证：
    ```
    cd /mnt/hgfs/share2.0/Ubantudemo
    mkdir -p build && cd build
    cmake .. && make
    ```

# Task Dependencies
- Task 2 依赖 Task 1（按钮和前向声明必须先存在）
- Task 3 独立（仅验证已有代码）
- Task 4 依赖 Task 1 和 Task 2 完成

# 关键设计决策
1. **远程文件优先拦截**：只要 `g_remote_sel_count > 0` 就弹窗，不管是否同时选了本地文件。这样逻辑最简单，避免歧义。
2. **目录跳过而非报错**：选中的目录条目（以 "/" 结尾）直接跳过，不中断批量删除流程。
3. **路径拼接与 `scan_local_directory` 保持一致**：根目录用 `./client/{filename}`，子目录用 `./client/{g_local_cur_path}/{filename}`。
4. **提前清空选中状态**：在调用 `ui_refresh_local_files()` 之前就清空 `g_selected_local`，避免刷新过程中引用已删除的文件名。
5. **`remove()` 而非 `unlink()`**：`remove()` 可同时处理文件和空目录，且声明在 `<stdio.h>` 中，无需额外 include。
