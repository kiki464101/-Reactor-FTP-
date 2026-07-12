# Checklist

## 递归删除辅助函数
- [ ] `remove_recursive(const char *path)` 已实现
- [ ] `stat(path)` 检查类型
- [ ] 目录 → `opendir` + `readdir` 遍历，跳过 "." 和 ".."，递归调用，`closedir`，最后 `rmdir`
- [ ] 文件 → `unlink(path)`
- [ ] 返回 0 成功，-1 失败
- [ ] `#include <unistd.h>` 已添加（如需要）

## execute_local_delete 支持文件夹
- [ ] 删除 "skip directory entries" 逻辑
- [ ] 对每个选中条目调用 `remove_recursive(path)`
- [ ] 路径构建 `./client/<name>` 正确
- [ ] 文件夹条目（尾部 "/"）能正确删除
- [ ] 成功/失败计数正确
- [ ] 删除后刷新本地列表
- [ ] 删除 server 文件夹仍弹窗 "error delete"

## start_download 路径统一
- [ ] 删除 `is_folder_dl` 分支
- [ ] 删除 `strchr(filename, '/')` 判断
- [ ] 统一使用 `./client/load/%s` 路径
- [ ] `mkdir_p` 正确创建子目录（如 `./client/load/myfolder/sub/`）
- [ ] 普通文件下载仍到 `./client/load/<filename>`
- [ ] 文件夹下载到 `./client/load/<文件夹名>/<文件路径>`

## ".." 返回上级验证
- [ ] 下载文件夹到 `./client/load/<文件夹名>/` 后进入 `load/` 有 ".."
- [ ] 进入 `<文件夹名>/` 有 ".." 返回 `load/`
- [ ] `scan_local_directory` 在子目录时添加 ".."（已有逻辑，验证）
- [ ] `on_local_file_item_clicked` 点击 ".." 返回上级（已有逻辑，验证）
- [ ] 远程列表 ".." 依赖 `add-server-folder-navigation` spec（标注依赖，本 spec 不实现）

## 文件夹删除验证
- [ ] 长按选中客户端文件夹 → Delete → Yes → 文件夹及所有内容删除
- [ ] 包含子文件夹的文件夹能递归删除
- [ ] 删除后本地列表刷新，不再显示该文件夹
- [ ] 长按选中 server 文件夹 → Delete → 弹窗 "error delete"

## 文件夹下载验证
- [ ] 长按选中 server 文件夹 → Download → 文件下载到 `./client/load/<文件夹名>/`
- [ ] 进入 `load/` → `<文件夹名>/` 可看到下载的文件
- [ ] 每层目录都有 ".." 返回上级
- [ ] 下载重复文件夹 → 弹窗 "Dirent has exist"
- [ ] 点击 close 关闭弹窗，正常进行其它操作

## 文件夹上传验证
- [ ] 长按选中本地文件夹 → Upload → server 端创建 `./copy/<文件夹名>/`
- [ ] 上传重复文件夹 → 弹窗 "Dirent has exist"
- [ ] 点击 close 关闭弹窗，正常进行其它操作
- [ ] NOTE：上传后进入 server 端文件夹查看内容依赖 `add-server-folder-navigation` spec

## 进度条和 cancel 验证
- [ ] 上传/下载时进度条弹窗显示
- [ ] 进度条随传输进度变化
- [ ] 点击 close 隐藏弹窗，传输继续
- [ ] 点击 cancel 取消整个文件夹传输
- [ ] cancel 后任务从队列中剔除

## 编译
- [ ] 代码审查通过
- [ ] 客户端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 无新增编译警告
