# Checklist

## 文件名 basename 提取
- [ ] `network_cmd_put()` 中提取 basename 的代码已添加
- [ ] `build_cmd_put` 使用 basename 而非完整路径
- [ ] `g_ul_filename` 仍存完整路径（本地读取用）

## 功能验证
- [ ] 上传 `./client/test_delete1.txt/test2.txt`，服务器 `./copy/` 下只出现 `test2.txt`（不是目录）
- [ ] 上传根目录文件（如 `hello.txt`），服务器 `./copy/hello.txt` 正常
- [ ] 上传子目录文件后，服务器 LS 列表中只显示文件名（不含子目录前缀）
- [ ] 上传子目录文件后，本地进度条和状态显示仍正常

## 编译
- [ ] 代码审查通过
- [ ] 客户端编译通过无错误
- [ ] 无新增编译警告
