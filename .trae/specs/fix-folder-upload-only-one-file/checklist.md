# Checklist

## collect_files_recursive 调试日志
- [ ] 函数入口添加日志输出scan_dir路径
- [ ] opendir失败时添加错误日志
- [ ] 收集每个文件时添加日志输出文件名
- [ ] 递归调用时添加日志输出rel_prefix

## collect_files_recursive 逻辑验证
- [ ] opendir正确打开目录
- [ ] readdir正确遍历所有条目
- [ ] 跳过"."和".."条目
- [ ] 递归调用时rel_prefix参数正确传递
- [ ] 文件路径拼接正确（rel_prefix + d_name）
- [ ] MAX_SELECTED_FILES限制不会导致提前退出

## 修复效果验证
- [ ] 客户端编译通过无错误
- [ ] 上传包含多个文件的文件夹成功
- [ ] 服务器端显示完整的文件夹结构
- [ ] 所有文件都被正确上传
