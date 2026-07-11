# Checklist

## 按钮布局
- [x] Delete 按钮出现在主界面按钮行，位于 Upload 和 Disconnect 之间
- [x] 按钮顺序为：Refresh | Download | Upload | Delete | Disconnect
- [x] `on_delete_btn_clicked` 前向声明已添加

## 远程文件删除拦截
- [x] 选择远程文件后点击 Delete，弹出 "error delete" 弹窗
- [x] 弹窗不会重复创建（单例保护 `err_popup && lv_obj_is_valid`）
- [x] 不执行任何删除操作
- [x] 点击弹窗的 Close 按钮后弹窗消失，`err_popup` 置 NULL
- [x] 弹窗关闭后可以正常进行其它操作（弹窗不阻塞主循环）

## 本地文件删除
- [x] 选择本地文件后点击 Delete，文件被成功删除
- [x] 删除后本地文件列表自动刷新，不再显示已删除的文件
- [x] 选中状态被清空，选中计数标签更新为 "Remote: 0 | Local: 0"
- [x] 在子目录中浏览时，删除文件的路径拼接正确（`./client/{g_local_cur_path}/{filename}`）
- [x] 在根目录浏览时，删除文件的路径拼接正确（`./client/{filename}`）

## 边界情况
- [x] 未选择任何文件时点击 Delete，状态栏显示 "No file selected"
- [x] 选中的目录条目（以 "/" 结尾）被跳过，不执行删除
- [x] 批量删除多个文件时全部成功，状态栏显示 "Deleted"
- [x] 批量删除中部分失败时，状态栏显示 "Some files failed to delete"

## 编译
- [x] 代码审查通过：所有函数引用、变量引用、头文件包含均正确
- [ ] 客户端编译通过无错误（需用户在 Linux 虚拟机中验证）
- [ ] 无新增编译警告（需用户在 Linux 虚拟机中验证）
