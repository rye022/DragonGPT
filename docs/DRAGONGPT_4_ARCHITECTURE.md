# DragonGPT 4.0 实施记录

基线：2026-10-26 独立审查修复后的 3.0 源码；当前目录原为空仓库。已导入代码、角色资源与测试，未复制用户数据库、凭据或运行缓存。基线文件哈希保存在 work/baseline-source.json；Git 基线可回滚。

## 审查结论

- C++20、Qt 6 Widgets、MinGW，原生 sprite/mesh 动画；没有已绑定的 Live2D Cubism 模型。保留原渲染和全部角色资源。
- 原 ChatService 使用 DeepSeek Responses SSE；SmartPage 使用 ToolHub 的结构化 JSON 静态计划，尚无模型观察工具结果后再规划的循环。
- ToolHub 已实现参数检查、超时、取消、幂等重试。FileOrganizer 已实现扫描、哈希、预览、移动日志及撤销。ScreenCapture/OCR 已有授权和敏感窗口保护。继续复用。
- ASR 是 Windows SAPI Language=804，自己管理麦克风；首个 recognition 事件立即 stopListening 并发送。没有假设/最终分离、自然停顿聚合、编辑确认和可选现代引擎。
- TTS 已经有本地 SAPI、离线 Qwen、百炼、缓存与 SpeechPlayer。大部分路径会停麦克风，但录音恢复没有回声尾部保护。
- 长期记忆、情绪、亲密度、主动对话由 Companion 持有；新增 Agent 不获得这些模块的隐式文件权限，工具结果默认只暂存任务上下文。
- 耗时文件工作使用 QtConcurrent，网络异步。新增工具进程与 ASR 推理进程隔离，GUI 不同步等待。单个 Agent 和单个 ASR 请求有显式超时/取消。

## 实施顺序

1. SAPI 假设/最终结果分离、多句聚合、设备释放、超时、回声尾部隔离。
2. 独立 SpeechRecognitionService，词库原文留存、最终稿编辑、明确的可靠性信号；可选 PCM/VAD 与本地/云端适配。
3. 复用 ToolHub 注册表与现有工具；新增原生 Chat Completions tool_calls 循环、工作区授权、逐操作批准、暂停/取消/审计。
4. 隔离 worker 提供真实文件、网页、浏览器和受限开发命令；Windows UIA 结构化操作。危险参数不接受模型自带的 approved 字段。
5. 本地夹具、权限绕过、真实临时文件、浏览器与回归测试；构建 4.0，输出手册和测试边界。没有音频标注则不计算真实 CER/WER；没有本地模型就不宣称 GPU 推理通过。

DeepSeek 原生协议依据：https://api-docs.deepseek.com/guides/tool_calls/ 。维持原聊天 Responses 接口，Agent 单独使用官方 Chat Completions 工具协议；不把自由文本当命令。
