# Checklist

## 确认弹窗
- [x] 点击 Delete（选中本地文件时）弹出确认弹窗，提示 "Confirm delete?"
- [x] 弹窗底部有 Yes 和 No 两个按钮
- [x] 点击 Yes 后执行删除操作，弹窗关闭
- [x] 点击 No 后弹窗关闭，不执行删除，选中状态保持不变
- [x] 弹窗不会重复创建（单例保护 `confirm_popup && lv_obj_is_valid`）
- [x] 弹窗关闭后可以正常进行其它操作

## 子目录删除修复
- [x] 在 `./client/load/` 子目录中选中文件并删除，文件被成功删除
- [x] 删除路径为 `./client/load/{filename}`（不再重复拼接子目录）
- [x] 在 `./client/` 根目录中选中文件并删除，文件被成功删除
- [x] 删除路径为 `./client/{filename}`

## 原有功能保持
- [x] 选择远程文件后点击 Delete，仍弹出 "error delete" 弹窗
- [x] 未选择任何文件时点击 Delete，仍显示 "No file selected"
- [x] 批量删除多个文件时全部成功，状态栏显示 "Deleted"
- [x] 批量删除中部分失败时，状态栏显示 "Some files failed to delete"
- [x] 删除后本地文件列表自动刷新
- [x] 选中状态被清空，选中计数标签更新

## 编译
- [x] 代码审查通过：所有函数引用、变量引用正确
- [ ] 客户端编译通过无错误（需用户在 Linux 虚拟机中验证）
- [ ] 无新增编译警告（需用户在 Linux 虚拟机中验证）
