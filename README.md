# DragonGPT 3.0 · 霜翎

Windows / Qt Widgets / C++20 桌面伙伴。保留固定Q版白龙娘、原动作与用户数据；支持本地陪伴、DeepSeek聊天、所选音色、记忆、剪贴板、专注、待办、电脑健康、文件预览和按需读屏。

- [完整使用手册](docs/DragonGPT_3.0_完整使用手册.md)
- [功能清单与验证边界](docs/DragonGPT_3.0_功能清单.md)
- [模块依赖与调用流程](docs/DragonGPT_3.0_模块依赖关系.md)
- [最终审查报告](docs/DragonGPT_3.0_最终审查报告.md)
- [测试、性能及未验证内容](docs/DragonGPT_3.0_测试结果.md)

本目录是2026-10-26—27的独立审查修复副本，应用版本仍为3.0.0。是否达到发布标准以本次报告为准；没有据此更新现有安装或远程Release。普通使用请解压完整运行包，运行DragonGPT.exe；默认离线，识屏与剪贴板记录默认关闭。

开发构建需Qt6（Core/Gui/Widgets/Sql/Network/Multimedia/WebSockets/Concurrent/Test）、CMake 3.24+、Ninja与MinGW C++20：

```powershell
./build.ps1 -QtRoot '你的Qt MinGW SDK目录' -CompilerDir '你的MinGW bin目录' -BuildDir './build' -Package
```

测试使用隔离数据与本地网络夹具；可选向量模型需通过CMake的DRAGON_EMBEDDING_TEST_HOME指定已准备的目录，缺失时相关模型子测试会明确跳过。请勿提交用户数据库、INI、DPAPI凭据、缓存、机器ready.json或模型权重。第三方许可见[THIRD_PARTY](THIRD_PARTY.md)。
