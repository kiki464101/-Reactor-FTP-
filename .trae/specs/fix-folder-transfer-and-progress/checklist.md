# Checklist

## 刷新根目录接口
- [ ] `ui_refresh_local_files_root()` 已实现（重置 `g_local_cur_path` + 刷新）
- [ ] `ui_refresh_remote_list_root()` 已实现（重置 `g_remote_cur_path` + `network_cmd_ls(NULL)`）
- [ ] 两个接口已声明在 `ui_manager.h`

## 刷新回调
- [ ] `cb_refresh_local_list` 调用 `ui_refresh_local_files_root()`
- [ ] `cb_refresh_remote_list` 调用 `ui_refresh_remote_list_root()`

## 防重入
- [ ] `network_cmd_get_multi` 入口检查 `g_batch_active && g_tx_queue.count > 0`
- [ ] 防重入触发时弹窗 "transfer in progress"
- [ ] 防重入触发时返回 false
- [ ] `network_cmd_put_multi` 入口检查 `g_batch_active && g_tx_queue.count > 0`
- [ ] 首次下载/上传（`g_batch_active` 为 false）正常执行

## 文件夹下载显示验证
- [ ] 下载 server 端文件夹 → 完成后本地根目录列表显示文件夹条目
- [ ] 点击本地文件夹 → 进入 → 显示文件夹下文件
- [ ] 点击 ".." → 返回根目录
- [ ] 普通文件下载仍到 `./client/load/`

## 文件夹上传显示验证
- [ ] 上传本地文件夹 → 完成后远程根目录列表显示文件夹条目
- [ ] 点击远程文件夹 → 进入 → 显示文件夹下文件
- [ ] 点击 ".." → 返回根目录

## 防重入验证
- [ ] 传输过程中点击下载 → 弹窗 "transfer in progress"
- [ ] 传输过程中点击上传 → 弹窗 "transfer in progress"
- [ ] 传输不受影响，继续进行

## 编译
- [ ] 代码审查通过
- [ ] 客户端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 无新增编译警告
