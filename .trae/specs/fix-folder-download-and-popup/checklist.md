# Checklist

## 移除 start_download 重复检测
- [ ] `start_download` 删除 `ui_local_list_has_entry(filename)` 检查
- [ ] `start_download` 删除 "repeat file" 弹窗
- [ ] `start_download` 删除 `g_state = ST_IDLE; return;`
- [ ] `start_upload` 同理移除重复检测（如有）
- [ ] 重复检测统一在 `network_cmd_get_multi`/`network_cmd_put_multi` 入队前完成

## 弹窗套用修复
- [ ] `on_download_btn_clicked` 移除 `ui_show_progress_batch()` 同步调用
- [ ] `on_upload_btn_clicked` 移除 `ui_show_progress_batch()` 同步调用
- [ ] `ui_show_error_popup` 入口隐藏 `batch_prog_panel`（若可见）
- [ ] `cb_show_progress_batch` 异步回调已实现
- [ ] `batch_start_next` 第一个任务开始时触发 `lv_async_call(cb_show_progress_batch, NULL)`
- [ ] 下载文件夹时仅显示一个进度条弹窗
- [ ] 下载重复文件夹时仅显示 "Dirent has exist" 弹窗（无进度条弹窗）
- [ ] 传输出错时错误弹窗隐藏进度条弹窗

## network_cmd_get_multi 文件夹重复检测
- [ ] 文件夹条目（尾部 "/"）提取 basename
- [ ] `ui_local_list_has_entry(basename)` 检查重复
- [ ] 重复时弹窗 "Dirent has exist"
- [ ] 重复时 `continue` 跳过，不发送 LISTDIR
- [ ] 不重复时调用 `network_cmd_listdir`
- [ ] 普通文件条目重复检测保持现有逻辑

## 文件夹下载完整性
- [ ] server 端 `./copy/hahahah/siusiu.txt` 文件存在（用户检查）
- [ ] `listdir_recursive` 返回 `hahahah/siusiu.txt` 路径
- [ ] `ST_WAIT_LISTDIR_RESP` 解析所有 token 并入队
- [ ] `batch_start_next` 逐个处理队列中所有任务
- [ ] `start_download` 路径 `./client/load/hahahah/siusiu.txt` 正确
- [ ] `mkdir_p` 创建 `./client/load/hahahah/` 子目录
- [ ] 所有文件都下载到 `./client/load/hahahah/`

## 下载显示验证
- [ ] 下载文件夹后 `./client/load/hahahah/` 下有所有文件
- [ ] 进入 `load/` → `hahahah/` 可看到所有下载的文件
- [ ] 每层目录有 ".." 返回上级
- [ ] 下载重复文件夹 → 弹窗 "Dirent has exist"
- [ ] 点击 close 关闭弹窗，正常进行其它操作

## 进度条和 cancel 验证
- [ ] 进度条弹窗随传输进度更新
- [ ] 点击 close 隐藏弹窗，传输继续
- [ ] 点击 cancel 取消整个文件夹传输
- [ ] cancel 后任务从队列中剔除

## 编译
- [ ] 代码审查通过
- [ ] 客户端编译通过无错误（用户在 Linux 虚拟机验证）
- [ ] 无新增编译警告
