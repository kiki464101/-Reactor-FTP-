# Tasks

所有修改集中在 `client/ui_manager.c` 一个文件中。

- [x] Task 1: 新增确认弹窗相关的前向声明和静态变量
  - [x] 1.1 在前向声明区（`on_delete_btn_clicked` 之后）添加：
    ```
    static void on_delete_confirm_yes_btn_clicked(lv_event_t *e);
    static void on_delete_confirm_no_btn_clicked(lv_event_t *e);
    static void execute_local_delete(void);
    ```
  - [x] 1.2 在 error popup 静态变量附近添加确认弹窗变量：
    ```
    static lv_obj_t *confirm_popup = NULL;
    ```

- [x] Task 2: 实现 `ui_show_confirm_popup()` 确认弹窗函数
  - [x] 2.1 在 `ui_show_error_popup()` 函数之后添加新函数，逻辑如下：
    - 单例保护：如果 `confirm_popup` 已存在且有效，直接 return
    - 创建弹窗容器（280x150），居中，红色边框
    - 使用 flex column 布局
    - 添加提示文本 "Confirm delete?"
    - 添加一行 flex row，包含 Yes 和 No 两个按钮（各 80x28）
    - Yes 按钮绑定 `on_delete_confirm_yes_btn_clicked`
    - No 按钮绑定 `on_delete_confirm_no_btn_clicked`

- [x] Task 3: 实现 Yes/No 按钮回调
  - [x] 3.1 实现 `on_delete_confirm_yes_btn_clicked()`：
    - 调用 `execute_local_delete()` 执行删除
    - 销毁弹窗：`lv_obj_del(confirm_popup); confirm_popup = NULL;`
  - [x] 3.2 实现 `on_delete_confirm_no_btn_clicked()`：
    - 仅销毁弹窗：`lv_obj_del(confirm_popup); confirm_popup = NULL;`
    - 不执行删除，不修改选中状态

- [x] Task 4: 实现 `execute_local_delete()` 函数（从原 `on_delete_btn_clicked` 提取）
  - [x] 4.1 将原 `on_delete_btn_clicked` 中第 786-832 行的删除逻辑提取为独立函数 `execute_local_delete()`
  - [x] 4.2 **修复路径拼接 bug**：将原来的
    ```
    if (g_local_cur_path[0])
        snprintf(path, sizeof(path), "./client/%s/%s", g_local_cur_path, name);
    else
        snprintf(path, sizeof(path), "./client/%s", name);
    ```
    改为
    ```
    snprintf(path, sizeof(path), "./client/%s", name);
    ```
    原因：`g_selected_local[i]` 在 `on_local_file_item_clicked` 中已存储完整相对路径（如 `"load/haha.txt"`），不应再拼接 `g_local_cur_path`，否则路径变成 `./client/load/load/haha.txt`（重复）
  - 其余逻辑（跳过目录条目、remove、清空选中、刷新、显示结果）保持不变

- [x] Task 5: 重构 `on_delete_btn_clicked()`
  - [x] 5.1 修改 `on_delete_btn_clicked` 逻辑为：
    - 选中远程文件 → `ui_show_error_popup("error delete")`，return（不变）
    - 未选中本地文件 → `ui_show_error("No file selected")`，return（不变）
    - 选中本地文件 → 调用 `ui_show_confirm_popup("Confirm delete?")`，return（**新增**，不再直接删除）
  - [x] 5.2 删除原函数中的删除逻辑（已移至 `execute_local_delete()`）

- [x] Task 6: 编译验证（代码审查通过，需用户在 Linux 虚拟机中编译）
  - [x] 6.1 代码审查：确认所有函数引用、变量引用正确
  - [ ] 6.2 用户在 Linux 虚拟机中编译验证

# Task Dependencies
- Task 2 依赖 Task 1（变量和声明必须先存在）
- Task 3 依赖 Task 1 和 Task 4（Yes 回调需要调用 execute_local_delete）
- Task 4 独立（纯逻辑提取）
- Task 5 依赖 Task 2 和 Task 4（需要确认弹窗和删除函数都就绪）
- Task 6 依赖 Task 1-5 全部完成

# 关键设计决策
1. **确认弹窗与错误弹窗分离**：使用独立的 `confirm_popup` 变量，不复用 `err_popup`，避免状态冲突。
2. **路径修复核心**：`g_selected_local[i]` 存的是完整相对路径（如 `"load/haha.txt"`），与 `normalize_local_path()` 在 network_task.c 中的处理方式一致——直接 `./client/%s` 拼接即可。
3. **选中状态保持**：点击 No 时不清空选中状态，用户可以重新操作或取消选择。
4. **弹窗单例保护**：与 `ui_show_error_popup` 一致，防止重复创建。
