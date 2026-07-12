# Tasks

- [ ] Task 1: 在collect_files_recursive函数中添加调试日志
  - [ ] SubTask 1.1: 在函数入口添加日志，输出scan_dir路径
  - [ ] SubTask 1.2: 在opendir失败时添加错误日志
  - [ ] SubTask 1.3: 在收集每个文件时添加日志，输出文件名和rel_prefix
  - [ ] SubTask 1.4: 在递归调用时添加日志，输出子目录名和新的rel_prefix

- [ ] Task 2: 检查并修复collect_files_recursive函数逻辑
  - [ ] SubTask 2.1: 验证opendir是否正确打开目录
  - [ ] SubTask 2.2: 验证readdir是否正确遍历所有条目（跳过.和..）
  - [ ] SubTask 2.3: 验证递归调用时rel_prefix参数正确传递
  - [ ] SubTask 2.4: 验证文件路径拼接正确（rel_prefix + d_name）
  - [ ] SubTask 2.5: 检查MAX_SELECTED_FILES限制是否导致提前退出

- [ ] Task 3: 验证修复效果
  - [ ] SubTask 3.1: 编译客户端代码无错误
  - [ ] SubTask 3.2: 测试上传包含多个文件的文件夹
  - [ ] SubTask 3.3: 验证服务器端显示完整的文件夹结构

# Task Dependencies
- Task 2 依赖 Task 1（先添加日志再修复问题）
- Task 3 依赖 Task 2（修复后验证效果）
