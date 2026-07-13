# Checklist

## network_cmd_put使用write_all
- [ ] 第1598行`write`改为`write_all`
- [ ] 检查`write_all`返回值，<0则`free(pkt); return false`
- [ ] PUT命令包完整发送到服务器

## network_cmd_put状态设置顺序
- [ ] `g_ul_filename`、`g_ul_local_path`、`g_state = ST_WAIT_PUT_RESP`移到重复检查之后
- [ ] 重复检查失败时`g_state`保持`ST_IDLE`
- [ ] 重复检查失败时`network_cmd_put`返回false

## 服务端send_packet使用write_all
- [ ] `protocol.c`新增`write_all`函数
- [ ] 第93行`write`改为`write_all`
- [ ] ACK/DONE包完整发送

## start_upload调试日志
- [ ] 打印`local_path`、`g_ul_fd`、`g_ul_total`
- [ ] `g_ul_total <= 0`时打印警告

## handle_upload_chunk调试日志
- [ ] 每次read后打印`r`（读取字节数）
- [ ] 每次write_all后打印`w`（写入字节数）、`g_ul_sent`、`g_ul_total`

## 编译和功能验证
- [ ] 客户端编译通过无错误
- [ ] 服务端编译通过无错误
- [ ] 上传.c文件，调试日志显示`g_ul_total`为正确文件大小
- [ ] 上传.c文件，服务器端文件内容与客户端完全一致
- [ ] 上传大文件内容完整
- [ ] 重复文件检查失败后状态机正常
