# Checklist

## UI 展示文件名镜像数组
- [ ] `g_remote_displayed_files[]` 和 `g_remote_displayed_count` 已定义
- [ ] `g_local_displayed_files[]` 和 `g_local_displayed_count` 已定义
- [ ] `ui_update_file_list_cb()` 中填充远程镜像数组（跳过 "." 和 ".." ）
- [ ] `ui_update_local_file_list_cb()` 中填充本地镜像数组（跳过头文本、 ".." 、目录条目）
- [ ] 每次列表刷新前 count 先重置为 0，避免残留旧数据

## 查询函数
- [ ] `ui_remote_list_has_file()` 已实现并声明在 `ui_manager.h`
- [ ] `ui_local_list_has_file()` 已实现并声明在 `ui_manager.h`
- [ ] 传入 NULL 时安全返回 false
- [ ] 大小写敏感匹配（与文件系统一致）

## 上传重复检测
- [ ] `network_cmd_put()` 使用 `ui_remote_list_has_file(base)` 检测
- [ ] `network_cmd_put_multi()` 使用 `ui_remote_list_has_file(base)` 检测
- [ ] 多文件上传重复文件用 `continue` 跳过，不中断批次
- [ ] basename 提取逻辑保留（strrchr 提取最后一段）
- [ ] 检测到重复时弹窗显示 "repeat file"
- [ ] 检测到重复时不发送 PUT 指令
- [ ] 上传新文件（远程列表无同名）时正常执行

## 下载重复检测
- [ ] `start_download()` 使用 `ui_local_list_has_file(filename)` 检测
- [ ] 检测到重复时弹窗显示 "repeat file"
- [ ] 检测到重复时设置 `g_state = ST_IDLE` 并返回
- [ ] 不创建/覆盖本地文件
- [ ] 下载新文件（本地列表无同名）时正常执行

## 弹窗行为
- [ ] 弹窗显示 "repeat file" 文本（三处统一）
- [ ] 弹窗有 Close 按钮
- [ ] 点击 Close 后弹窗销毁
- [ ] 点击 Close 后可正常进行其它操作
- [ ] 弹窗不重复创建（单例保护）

## 清理
- [ ] `network_task.c` 中 `check_file_exists` 两处重复定义已删除
- [ ] 无遗留 `check_file_exists` 调用引用
- [ ] 无遗留 "file have exist" 文案

## 编译
- [ ] 代码审查通过
- [ ] 客户端编译通过无错误
- [ ] 无新增编译警告
