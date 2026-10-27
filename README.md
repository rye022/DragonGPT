# DragonGPT 4.0 · 霜翎

Windows / Qt 6 / C++20 桌面伙伴，保留原 Q 版白龙娘、动作、情绪、亲密度、记忆、主动互动和所选 TTS 音色。4.0 增加原生 DeepSeek Tool Calling、工作区授权和逐操作批准、真实文件/网页/浏览器/Windows UIA 工具、受限开发预设，以及可编辑最终稿的独立语音识别服务。

- [4.0 使用手册](docs/DRAGONGPT_4_MANUAL.md)：功能、语音配置、Agent 权限、例子、依赖准备和故障排查。
- [架构审查与实施记录](docs/DRAGONGPT_4_ARCHITECTURE.md)
- [测试报告和未验证项](docs/DRAGONGPT_4_TEST_REPORT.md)
- [版本说明](docs/RELEASE_v4.0.0.md)

普通聊天为默认模式，Agent 目录授权只对当前会话有效。危险操作展示完整参数后逐项批准。网页/文件内容不能授予权限；未支持或失败的工具返回真实错误。CMake/测试是经批准后以用户权限执行的代码，不是操作系统沙箱。

原生动画不是已绑定 Cubism 模型。SAPI、本地 faster-whisper 和云端兼容转写接口互相独立于 TTS；默认保留 SAPI。CPU 本地识别已测试，本机 CUDA 推理仍缺运行库，云端 ASR 真实计费调用和真人麦克风准确率尚未验收。请以测试报告的边界为准。

解压完整 Windows 运行包运行 DragonGPT.exe。可选工具/ASR 依赖需在目标机器准备：

```powershell
python agent/prepare.py --with-asr --model base
```

本地准备会明确安装依赖和下载模型，Agent 不会自动运行此脚本。也可在 UI 选择现有 Python / 模型目录。浏览器依赖本机 Edge。

开发构建需要 Qt 6.5+ 的 Core/Gui/Widgets/Sql/Network/Multimedia/WebSockets/Concurrent/Test、CMake 3.24+、Ninja 和 MinGW C++20：

```powershell
./build.ps1 -QtRoot 'Qt MinGW SDK目录' -CompilerDir 'MinGW bin目录' -BuildDir './build' -Package
```

测试使用隔离数据、临时文件和本地协议夹具。不得提交数据库、INI、DPAPI 凭据、ready.json、Python 环境、模型权重或个人日志。升级不覆盖用户数据或远程正式 Release。许可见 [THIRD_PARTY](THIRD_PARTY.md)。
