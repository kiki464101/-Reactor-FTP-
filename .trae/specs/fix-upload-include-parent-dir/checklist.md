# Checklist

## transfer_task_t结构体
- [ ] 新增 `char local_path[520]` 字段
- [ ] 字段注释为"本地完整读取路径"

## network_cmd_put函数
- [ ] 函数签名改为 `bool network_cmd_put(const char *filename, const char *local_path)`
- [ ] 用 `local_path` 调用 `stat` 检查文件存在
- [ ] 用 `local_path` 调用 `open` 打开文件（在start_upload中）
- [ ] 用 `filename` 发送PUT命令（`build_cmd_put(filename, ...)`）
- [ ] 用 `filename` 提取basename进行重复检查
- [ ] `g_ul_filename` 存储服务器端文件名 `filename`
- [ ] `network_task.h` 中声明同步更新

## start_upload函数
- [ ] 函数签名改为 `start_upload(const char *filename, const char *local_path)`
- [ ] 用 `local_path` 调用 `open(local_path, O_RDONLY)` 打开文件
- [ ] `g_ul_filename` 存储服务器端文件名 `filename`

## batch_start_next函数
- [ ] 调用 `network_cmd_put(task.filename, task.local_path)` 传递两个参数

## network_cmd_put_multi入队逻辑
- [ ] 文件夹上传：`task.filename` = sub_prefix + "/" + relative_path（如 "testupload/file1.txt"）
- [ ] 文件夹上传：`task.local_path` = local_base + "/" + relative_path（如 "./client/load/testupload/file1.txt"）
- [ ] 普通文件上传：`task.filename` = basename（如 "hello.txt"）
- [ ] 普通文件上传：`task.local_path` = normalize_local_path(filenames[i])（如 "./client/load/hello.txt"）
- [ ] `task.filename` 不含父目录前缀（如 "load/"）
- [ ] `task.local_path` 包含完整本地路径

## local_path全局变量传递
- [ ] 新增 `static char g_ul_local_path[520]` 全局变量
- [ ] `network_cmd_put` 中存储 `local_path` 到 `g_ul_local_path`
- [ ] `ST_WAIT_PUT_RESP` 处理中调用 `start_upload(g_ul_filename, g_ul_local_path)`
- [ ] `finish_upload` 中重置 `g_ul_local_path[0] = '\0'`

## 编译和功能验证
- [ ] 客户端编译通过无错误
- [ ] 根目录上传普通文件，服务器端文件名正确
- [ ] 子目录上传普通文件，服务器端只显示basename
- [ ] 子目录上传文件夹，服务器端只创建选中的文件夹（不含父目录load/）
- [ ] 文件夹下所有文件内容完整传输
- [ ] 本地文件能正确读取（stat/open成功）
