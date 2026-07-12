# Tasks

- [ ] Task 1: 移除 `start_download` 重复检测
  - [ ] SubTask 1.1: `client/network_task.c` 修改 `start_download`（第 585-590 行）：
    - 删除 `if (ui_local_list_has_entry(filename)) { ... "repeat file" ... g_state = ST_IDLE; return; }` 整段
    - 重复检测改由 `network_cmd_get_multi` 入队前统一处理
  - [ ] SubTask 1.2: 同理移除 `start_upload` 中的重复检测（如有）

- [ ] Task 2: 弹窗套用修复
  - [ ] SubTask 2.1: `client/ui_manager.c` 修改 `on_download_btn_clicked`（第 813 行）：
    - 删除 `ui_show_progress_batch();` 同步调用
    - 进度条弹窗改为网络线程延迟创建
  - [ ] SubTask 2.2: `client/ui_manager.c` 修改 `on_upload_btn_clicked`（第 831 行）：
    - 同理删除 `ui_show_progress_batch();`
  - [ ] SubTask 2.3: `client/ui_manager.c` 修改 `ui_show_error_popup`（第 1028 行入口）：
    - 添加检查：若 `batch_prog_panel` 存在且可见 → `lv_obj_add_flag(batch_prog_panel, LV_OBJ_FLAG_HIDDEN)` 隐藏
    - 然后继续创建/更新错误弹窗
  - [ ] SubTask 2.4: `client/network_task.c` 新增 `cb_show_progress_batch(void *data)` 异步回调：
    - `(void)data; ui_show_progress_batch();`
  - [ ] SubTask 2.5: `client/network_task.c` 修改 `batch_start_next`：
    - 在第一个任务成功开始传输后（`g_batch_done == 0` 且 `g_state` 为传输状态）
    - 调用 `lv_async_call(cb_show_progress_batch, NULL)` 触发进度条弹窗创建

- [ ] Task 3: `network_cmd_get_multi` 文件夹重复检测
  - [ ] SubTask 3.1: `client/network_task.c` 修改 `network_cmd_get_multi`：
    - 文件夹条目（尾部 "/"）→ 提取 basename → `ui_local_list_has_entry(basename)` 检查
    - 重复 → `lv_async_call(cb_show_error_popup, make_str_data("Dirent has exist"))`，`continue` 跳过
    - 不重复 → 调用 `network_cmd_listdir` 发送 LISTDIR 请求
  - [ ] SubTask 3.2: 验证普通文件条目的重复检测保持现有逻辑（basename + `ui_local_list_has_entry`）

- [ ] Task 4: 验证文件夹下载完整性
  - [ ] SubTask 4.1: 确认 server 端 `./copy/hahahah/siusiu.txt` 文件存在（用户检查）
  - [ ] SubTask 4.2: 确认 `listdir_recursive` 返回 `hahahah/siusiu.txt` 路径（可加临时日志）
  - [ ] SubTask 4.3: 确认 `ST_WAIT_LISTDIR_RESP` 解析所有 token 并入队
  - [ ] SubTask 4.4: 确认 `batch_start_next` 逐个处理队列中的所有任务
  - [ ] SubTask 4.5: 确认 `start_download` 路径 `./client/load/hahahah/siusiu.txt` 正确
  - [ ] SubTask 4.6: 确认 `mkdir_p` 创建 `./client/load/hahahah/` 子目录

- [ ] Task 5: 验证 + 编译
  - [ ] SubTask 5.1: 下载文件夹 → 仅显示一个进度条弹窗（无错误弹窗叠加）
  - [ ] SubTask 5.2: 下载重复文件夹 → 仅显示 "Dirent has exist" 弹窗（无进度条弹窗）
  - [ ] SubTask 5.3: 下载文件夹后 `./client/load/hahahah/` 下有所有文件
  - [ ] SubTask 5.4: 进入 `load/` → `hahahah/` 可看到所有下载的文件
  - [ ] SubTask 5.5: 传输过程中出错 → 错误弹窗隐藏进度条弹窗，仅显示错误弹窗
  - [ ] SubTask 5.6: 客户端编译通过无错误（用户在 Linux 虚拟机验证）

# Task Dependencies
- Task 1 独立（移除重复检测）
- Task 2 独立（弹窗修复）
- Task 3 依赖 Task 1（重复检测移到 `network_cmd_get_multi`）
- Task 4 依赖 Task 1 和 Task 3（验证完整下载流程）
- Task 5 依赖所有前置任务

# 并行执行建议
- Task 1 + Task 2 可并行（修改不同函数）
- Task 3 依赖 Task 1
