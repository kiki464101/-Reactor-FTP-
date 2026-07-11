# Checklist

## 传输节流
- [ ] `CHUNK_DELAY_US` 宏已定义（建议 8000 微秒）
- [ ] `handle_download_chunk()` 中每个 chunk 后有 `usleep(CHUNK_DELAY_US)`
- [ ] `handle_upload_chunk()` 中每个 chunk 后有 `usleep(CHUNK_DELAY_US)`
- [ ] 传输速度减缓到肉眼可见进度条变化的程度

## 进度更新阈值
- [ ] `update_dl_progress()` 中阈值从 2% 降到 1%
- [ ] `update_ul_progress()` 中阈值从 2% 降到 1%
- [ ] 下载时进度条随传输进度可见地变化长度
- [ ] 上传时进度条随传输进度可见地变化长度

## Close 按钮
- [ ] 点击 Close 仅关闭进度弹窗，不停止传输
- [ ] 点击 Close 后传输在后台继续完成
- [ ] 点击 Close 后可以正常进行其它操作

## Cancel 按钮
- [ ] 点击 Cancel 取消文件传输
- [ ] 点击 Cancel 后弹窗关闭
- [ ] 点击 Cancel 后显示 "Transfer cancelled" 提示
- [ ] 点击 Cancel 后可以从任务队列中剔除该任务

## 编译
- [ ] 代码审查通过
- [ ] 客户端编译通过无错误
- [ ] 无新增编译警告
