# Checklist

## 重复文件检测辅助函数
- [x] `check_file_exists()` 函数已实现
- [x] 使用 `stat()` 检查文件是否存在

## 上传重复检测
- [x] `network_cmd_put()` 中提取 basename 后检测 `./copy/` 下是否重复
- [x] 检测到重复时弹出 "file have exist" 弹窗
- [x] 检测到重复时返回 false，不发送 PUT 指令
- [x] `network_cmd_put_multi()` 中对每个文件检测重复
- [x] 多文件上传时重复文件被跳过（continue），不中断批次
- [x] 上传新文件（无重复）时正常执行

## 下载重复检测
- [x] `start_download()` 中在 `open()` 前检测 `./client/load/` 下是否重复
- [x] 检测到重复时弹出 "file have exist" 弹窗
- [x] 检测到重复时设置 `g_state = ST_IDLE` 并返回
- [x] 不覆盖已存在的本地文件
- [x] 下载新文件（无重复）时正常执行

## 弹窗行为
- [x] 弹窗显示 "file have exist" 文本
- [x] 弹窗有 Close 按钮
- [x] 点击 Close 后弹窗销毁
- [x] 点击 Close 后可正常进行其它操作
- [x] 弹窗不重复创建（单例保护）

## 编译
- [x] 代码审查通过
- [x] 客户端编译通过无错误
- [x] 无新增编译警告
