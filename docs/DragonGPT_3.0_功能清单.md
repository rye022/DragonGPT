# DragonGPT 3.0 功能清单

审查日期：2026-10-26—27，Asia/Shanghai。基于本次 `outputs/DragonGPT` 源码，不把任务举例视作已实现。

共登记 **60 项功能/维护能力**。表中源码相对 `src/`，含斜杠的脚本路径相对项目根；测试名省略 `dragon_` 前缀。标有“源码”或“真实服务未验证”不等于运行验收通过。默认值针对全新隔离数据，旧用户配置优先。GUI自动化通过只说明覆盖到的路径。

| ID | 功能 | 类型 | 入口 | 条件与机制 | 新建数据默认值 | 网络 | AI依赖 | 源码位置 | 本次验证证据/边界 |
|---|---|---|---|---|---|---|---|---|---|
| F01 | 桌宠启动、显示与隐藏 | 混合 | 启动器、托盘、智能页快捷键 | 启动显示；隐藏停止动画帧计时 | 启用 | 否 | 否 | main.cpp;pet_widget.cpp | v3_ui,desktop_startup |
| F02 | 单击台词与动作联动 | 被动 | 单击可见角色 | 等待双击判定后，按当前动作选句；1100ms 防抖 | 启用 | 选云声且无缓存时 | 否（TTS独立） | pet_widget.cpp;voice_controller.cpp;dialogue_engine.cpp | actions,dialogue,api_audit |
| F03 | 双击聊天与拖动 | 被动 | 双击、左键拖动 | 双击不再额外触发单击；拖动约束显示器区域 | 启用 | 否 | 否 | pet_widget.cpp | desktop_startup,update |
| F04 | 位置恢复与多屏布局 | 混合 | 启动、拖动、正常退出 | 保存位置，启动检查屏幕可用区域 | 启用 | 否 | 否 | main.cpp;pet_widget.cpp | desktop_startup；混合DPI待人工 |
| F05 | 托盘入口与退出 | 被动 | 系统托盘 | 显示、隐藏、聊天、设置、识屏、退出 | 有托盘时启用 | 识屏/聊天按配置 | 按操作 | main.cpp | 源码；包冒烟 |
| F06 | Q 显隐与识屏快捷键 | 被动 | 桌面空白处双击Q；Ctrl+Alt+S | 普通Q透传；可改Ctrl+Q/Alt+Q/Ctrl+Alt+Q/off | Q；识屏须授权 | 识屏按配置 | 按识别方式 | desktop_hotkey.cpp;smart_page.cpp | intelligence；物理按键待人工 |
| F07 | 尺寸、置顶与半身镜头 | 被动 | 设置、右键镜头/大小菜单 | 260–680px滑杆；菜单320/480/600；裁切同一角色 | 480px、置顶、全身 | 否 | 否 | pet_widget.cpp;companion_window.cpp | update,special_blinks |
| F08 | 原动作与自然动画 | 混合 | 设置/右键动作；自动待机 | 陪伴、开心招手、害羞傲娇、鼓脸吃醋、抱枕休息、认真工作、抱尾陪伴；六张动作图+基础立绘 | 动画、随机与自动休息启用 | 否 | 否 | pet_actions.cpp;sprite_motion.cpp;pet_widget.cpp | actions,special_blinks,update |
| F09 | 动作优先级与恢复自动 | 混合 | 点击、拖动、聊天、专注、预览 | 单一PetWidget仲裁，禁止多个渲染器争抢 | 启用 | 否 | 否 | pet_widget.cpp | actions,emotion_ui,focus_ui |
| F10 | 情绪判断、衰减与场景反馈 | 主动 | 聊天、空闲/返回、专注阶段 | 14种规则情绪；短期反应、持久基线与滞后保护 | 启用 | 否 | 否 | emotion_engine.cpp;companion_life.cpp | emotion_context,emotion_ui |
| F11 | 主动开场与防打扰 | 主动 | 单次随机计时器 | 正常5–12分评估；90分冷却、每日3次；忽略退避、22–8时勿扰 | 启用 | 仅云声TTS可能联网 | 台词不用生成AI | companion.cpp;companion_life.cpp;voice_controller.cpp | life,intelligence,dialogue |
| F12 | 学习话题主动跟进 | 主动 | 记忆页单独授权 | 只用有限学习主题标签；每日最多一次、同记忆3日冷却 | 关闭 | 仅云声TTS可能联网 | 否 | memory_store.cpp;companion.cpp | semantic |
| F13 | 关系成长与阶段解锁 | 混合 | 聊天、单击、完成专注 | 去重、冷却、每日20经验上限；0/20/50/80%阶段 | 启用 | 否 | 否 | companion_life.cpp;memory_store.cpp | life,bond,focus |
| F14 | 羁绊图鉴、筛选与试听 | 被动 | 羁绊图鉴、顶部按钮 | 区分解锁、实际展示、首次听到；可隐藏未收集正文 | 启用 | 试听云声可能联网 | 否 | bond_gallery.cpp;growth_gauge.cpp | bond,upgrade |
| F15 | 开场试听 | 被动 | 陪伴→试听她会怎样开场 | 不计次数、收藏和成长；使用当前音色 | 按需 | 云声可能联网 | 否 | companion.cpp;voice_controller.cpp | bond,api_audit |
| F16 | 离线聊天、称呼与快捷话题 | 被动 | 对话；叫我…；快捷按钮 | 本地预设回复，明确标记离线来源 | 默认离线 | 否 | 否 | companion.cpp;companion_window.cpp | core,product,update |
| F17 | DeepSeek流式聊天 | 被动 | 连接启用后发送 | 有限历史/记忆、取消、超时、错误后恢复草稿；不自动重发付费请求 | 关闭 | 是 | 是 | chat_service.cpp;response_stream.cpp;companion.cpp | network,api_audit；真实生成未验证 |
| F18 | 轻量/复杂模型与代理 | 被动 | 连接、智能→模型 | 复杂关键词路由可选；共用ChatService并发闸门 | 复杂模型留空；直连 | 在线操作时 | 在线操作时 | chat_settings.cpp;chat_service.cpp;companion.cpp | network,intelligence,v3_ui |
| F19 | 用量和错误诊断 | 混合 | 连接页状态 | 保存收到的usage与最近限流头；不能代表账户余额 | 启用 | 随请求 | 随请求 | api_errors.cpp;memory_store.cpp;companion_window.cpp | api_audit,network |
| F20 | 模型列表与认证检查 | 被动 | 智能→读取可用模型/测试认证 | 仅请求/models，不发生成；声明image可记录视觉能力 | 按需 | 是 | 不生成 | smart_page.cpp | 源码；真实账户未请求 |
| F21 | 本地小脑指令 | 被动 | 对话输入完整命令 | 时间、星期、动作、CPU/内存、待办、总结、专注、喝水；保守拒绝引语/否定 | 启用 | 否 | 否 | local_brain.cpp;companion.cpp;local_tools.cpp | brain,tools |
| F22 | 手工记忆、搜索与编辑 | 被动 | 记忆页；记住：… | 内容、标签、重要性、到期、层次、来源、主动许可 | 按需 | 否 | 否 | memory_store.cpp;companion_window.cpp | core,semantic,product |
| F23 | 自动记忆建议与冲突确认 | 混合 | 聊天后建议→陪伴确认/忽略 | 本地短句规则提取；最多20条待确认；相反偏好确认替换 | 建议开启 | 否 | 否 | companion_life.cpp;memory_store.cpp | life,intelligence,final_audit |
| F24 | 关键词记忆与模型背景 | 混合 | 提问、在线发送 | 最多3相关、8资料、4交流偏好；编辑/删除立即生效 | 启用 | 在线时发送相关内容 | 在线回复使用 | companion.cpp;memory_store.cpp | product,network,semantic,final_audit |
| F25 | 本地语义向量检索 | 混合 | 已准备环境后聊天按需 | ONNX CPU，失败降级关键词；空闲60秒卸载worker | 运行环境不随包提供 | 运行时否；准备需下载 | 本地嵌入模型 | semantic_memory.cpp;embedding/worker.py | semantic（本机已有环境） |
| F26 | 记忆导入/导出 | 被动 | 记忆页JSON入口 | 导入≤2MiB/1000条；规范化去重跳过；事务回滚 | 按需 | 否 | 否 | memory_store.cpp;companion_window.cpp | product,semantic,final_audit |
| F27 | SQLite备份、恢复与迁移 | 被动/后台 | 记忆页备份；--restore-backup/--verify-data | schema7；旧库迁移前备份；恢复前另存回滚库 | 按需/必要时 | 否 | 否 | memory_store.cpp;main.cpp | semantic,upgrade |
| F28 | 清空长期记忆与清空档案 | 被动 | 记忆/设置页危险操作 | 须界面确认；仅清记忆与清档案范围不同，本次未对真实数据执行 | 按需 | 否 | 否 | memory_store.cpp;companion_window.cpp | semantic,core；真实删除未执行 |
| F29 | 当前音色TTS与试听 | 被动/混合 | 语音、点击、聊天朗读 | 系统中文、苏瑶、Audio TTS；试听与聊天共用选择 | 苏瑶；打字朗读关闭 | 云声需网络 | 云端语音模型可选 | voice_controller.cpp;qwen_speech.cpp;local_speech.cpp | api_audit,latency；主观音质待人工 |
| F30 | 情绪语调与语速 | 混合 | Audio TTS音色选择后自动 | 代码按情绪与关系设置rate/pitch/instruction；无独立自由语速滑杆 | 随支持音色 | 云声需网络 | 云声模型 | speech_expression.cpp;voice_controller.cpp | emotion_ui,api_audit |
| F31 | 流式分句、播放与互斥 | 后台 | 文本流、PCM到达 | 单控制器分句/有限预取；共享播放器；取消拒绝旧回调 | 随朗读 | 云声时 | 云声时 | speech_text_buffer.cpp;voice_controller.cpp;qwen_speech.cpp;audio_duplex.cpp | latency,api_audit,focus_ui |
| F32 | 语音通话、静音和打断 | 被动 | 语音页/右键开始语音 | Windows本地中文识别→现有聊天→所选TTS；结束关闭识音 | 关闭 | 在线聊天/云声时 | 按配置 | voice_controller.cpp;local_speech.cpp;voice_session.cpp | api_audit；真实麦克风识别待人工 |
| F33 | 语音缓存、备用音色 | 混合 | 语音→缓存/备用开关 | 200MiB上限；清缓存；新安装备用默认开，原声失败会显示来源 | 缓存、备用启用 | 缓存命中可离线 | 否 | offline_speech.cpp;voice_controller.cpp | api_audit,update |
| F34 | 可选离线苏瑶与准备脚本 | 被动/后台 | offline/install.ps1；语音优先离线 | 显式下载/准备；按需worker；60秒闲置卸载 | 包不含环境/权重 | 准备联网；合成离线 | 本地语音模型 | offline_speech.cpp;offline/worker.py;offline/prepare.py | 源码；本次新包无模型，真实合成未重跑 |
| F35 | 剪贴板历史记录与分类 | 混合 | 头顶剪贴板→记录开关 | 一个dataChanged监听；30ms去抖；过滤敏感内容 | 关闭 | 否 | 否 | clipboard_panel.cpp;clipboard_history.cpp | clipboard,clipboard_ui |
| F36 | 剪贴板复制、固定与清理 | 被动/后台 | 展开面板、双击/复制/固定/删除 | 未固定最多50、固定20；默认24小时；复制读回与最多5次重试 | 已有记录可操作 | 否 | 否 | clipboard_panel.cpp;clipboard_history.cpp | clipboard_ui,update |
| F37 | 剪贴板AI分析 | 被动 | 选中→用AI分析 | ≤1600字、逐次确认，进入正常聊天保存历史 | 按需 | 是 | 是 | companion_window.cpp;clipboard_panel.cpp | 源码/本地入口；真实分析未验证 |
| F38 | 硬件状态与十分钟趋势 | 主动/被动 | 工具→电脑健康；问CPU等 | 单HealthMonitor，5秒后台采集；GPU依API；CPU温度不支持 | 启动陪伴空间后运行 | 否 | 否 | health_monitor.cpp;tools_page.cpp | health,brain,focus_ui |
| F39 | 持续高负载提醒 | 主动 | CPU/内存≥90%或GPU≥95%持续2分 | 最少30分间隔；与提醒展示共用占用检查 | 启用 | 仅朗读可能 | 否 | health_monitor.cpp;companion_window.cpp | health；真实高负载不主动制造 |
| F40 | 番茄钟阶段与记录 | 混合 | 专注陪伴；本地命令；focus.start | 25/5/15分、4轮长休息；暂停/继续/结束/跳过/重置 | 手动开始；自动下一段关闭 | 否（朗读除外） | 否 | focus_service.cpp;focus_page.cpp | focus,focus_ui,v3_ui |
| F41 | 喝水提醒与确认动作 | 主动/被动 | 专注设置；气泡三个按钮；本地预约 | 默认活跃累计60分，专注推迟；确认/延后15分/今天静音 | 启用 | 仅云声朗读可能 | 否 | focus_service.cpp;focus_page.cpp;pet_widget.cpp | focus,focus_ui |
| F42 | 本地活跃、锁屏和恢复 | 后台 | 自动；专注页状态 | 5秒系统探针；不存按键正文/网站；锁屏/休眠暂停，重启需继续 | 启用 | 否 | 否 | focus_service.cpp;focus_page.cpp;health_monitor.cpp | focus,focus_ui |
| F43 | 待办CRUD与自然语言解析 | 被动 | 工具→待办；聊天；todo.create | 新增、完成/重开、删除、日期与优先级；有歧义需审核 | 按需 | 否 | 否 | task_parser.cpp;local_tools.cpp;tools_page.cpp;smart_page.cpp | tools,brain,v3_ui |
| F44 | 到期/逾期待办提醒 | 主动 | 每秒本地tick | notified标记去重；每批最多10；退出期间不通知、启动补查 | 有待办时 | 仅云声朗读可能 | 否 | local_tools.cpp;companion_window.cpp | tools |
| F45 | 每日统计、导出与AI回顾 | 混合 | 工具→每日总结；daily.summary | 复用健康活跃/专注/待办/互动；刷新/导出/手工存情景记忆 | 统计启用；AI回顾按需 | AI回顾/朗读时 | AI回顾时 | local_tools.cpp;tools_page.cpp;memory_store.cpp | tools,health,focus；真实回顾未验证 |
| F46 | 文件扫描、重复标记与移动 | 被动 | 工具→文件整理；files.preview | 单层≤5000文件；预览+确认移动；哈希、目录边界、不覆盖 | 按需 | 否 | 否（规则分类） | file_organizer.cpp;tools_page.cpp | files,v3_ui |
| F47 | 文件日志撤销与取消 | 被动 | 文件整理→撤销/取消 | 校验哈希及目标；失败尝试回滚，改变的文件拒绝覆盖 | 按需 | 否 | 否 | file_organizer.cpp;tools_page.cpp | files |
| F48 | 授权按需截图与窗口恢复 | 被动 | 智能、右键、托盘、Ctrl+Alt+S | 单捕获服务；外部helper可超时终止；恢复自身窗口 | 关闭 | 本地捕获否 | 否 | screen_capture.cpp;capture_helper.cpp;smart_page.cpp | intelligence,v3_ui；多屏待人工 |
| F49 | Windows OCR与临时结果 | 被动 | 智能→本地OCR | 系统语言包/PowerShell；25秒截止；结果60秒清除 | 识屏方式默认OCR | 离线只显示原文；在线文本推理 | 在线推理时 | screen_ocr.cpp;scripts/ocr.ps1;smart_page.cpp | intelligence（真实系统OCR） |
| F50 | 视觉提取、屏幕推理与缓存 | 被动 | 智能→视觉模型 | 代码校验图像能力；图片→事实→推理；同图同设置30秒内存缓存 | 须配置并授权 | 是 | 是 | smart_page.cpp;chat_service.cpp | intelligence协议；真实视觉未验证 |
| F51 | 本地情境分类与保护 | 主动/混合 | 智能单独开关且识屏总开关开启 | 每秒窗口标题、近期提问、按需事实；10秒稳定，会议/全屏立即保护 | 关闭 | 分类不联网 | 否 | context_engine.cpp;screen_capture.cpp;smart_page.cpp | intelligence |
| F52 | 工具注册、计划和执行 | 混合 | 智能→制定计划→审核执行 | 9工具、最多8步；白名单/参数/重复校验；顺序/取消/超时/有限只读重试 | 按需 | 模型规划时 | 复杂规划时 | tool_hub.cpp;smart_page.cpp | intelligence,v3_ui,final_audit |
| F53 | 小型资料总结 | 被动 | 智能目标→document.summary | 选择≤64KB UTF-8文件；审核≤12000字片段；临时结果 | 按需 | 在线总结时 | 在线总结时 | smart_page.cpp | 源码与调度；真实AI总结未验证 |
| F54 | 连接、凭据与本地设置 | 被动/后台 | 连接/语音/设置/陪伴/智能 | DPAPI；环境变量优先；分领域INI；保留原音色与配置 | 按配置 | 保存不联网 | 否 | chat_settings.cpp;qwen_speech.cpp;companion_window.cpp | network,api_audit,update |
| F55 | 单实例锁与本机控制 | 后台/被动 | 重复启动、--desktop/--hide-pet/--quit | 按数据目录锁与用户级QLocalServer，64字节命令上限 | 启用 | 本机IPC | 否 | main.cpp | desktop_startup；稳定性IPC |
| F56 | 日志、崩溃与延迟追踪 | 后台 | 数据目录logs | 状态/时序元数据，key掩码；app.log轮替约1MiB | 启用 | 否 | 否 | diagnostics.cpp;latency_trace.h | 源码、包冒烟；真实崩溃未制造 |
| F57 | CLI自检和公开短句预缓存 | 被动/开发 | --smoke-test、--voice-self-test、--offline-self-test、--prepare-click-voices | 预览/验证；公开短句预缓存会用配置key调用云端 | 仅显式命令 | 预缓存是 | 按命令 | main.cpp | smoke；收费预缓存未执行 |
| F58 | 安装、卸载、启动项与打包 | 被动 | EXE安装器或install.ps1；build.ps1 | 每用户安装；开始菜单；桌面/自启动可选；卸载保留用户库 | 启动项默认不勾选 | 可选模型下载时 | 否 | installer/DragonGPT.iss;install.ps1;uninstall.ps1;build.ps1;package_release.py | 便携包本次重测；EXE安装未重跑 |
| F59 | 遗留Realtime与音频双工适配 | 开发保留 | 无当前普通用户入口 | RealtimeClient/OpenAI协议、AudioDuplex保留并由夹具测试；当前VoiceController未连接它们 | 不自动运行 | 仅开发显式调用时 | 外部实时模型 | realtime_client.cpp;audio_duplex.cpp;voice_session.cpp | api_audit；真实Realtime未调用 |
| F60 | 显式模型准备与桌面归档脚本 | 维护入口 | embedding/offline安装；scripts/desktop_cleanup.ps1 | 模型下载需手工执行；归档按清单/哈希；不属后台自动整理 | 不自动运行 | 准备模型时 | 可选本地模型 | embedding/prepare.py;offline/prepare.py;scripts/desktop_cleanup.ps1 | 源码/语法；本次未下载或移动桌面文件 |

## 机械扫描与覆盖补充

- [全部按钮、菜单、标签和CLI入口扫描](audit/UI入口索引.md)。这是逐行入口索引，覆盖同一源码行里的多个控件；语义分类以本表为准。
- [全部文件哈希、include边、定时器、连接和符号索引](audit/source-index.json)。头文件接口与实现均列入；资源共42个文件、405条台词，以资源JSON为准。
- [原角色、资源和人格提示哈希核对](audit/asset-integrity.json)。原始资源无变动。
- 当前没有独立 GPU/CPU 故障诊断模型；没有后台循环截图；每日总结不自动向云端生成；长期记忆自动提取采用保守规则；文件分类是扩展名规则；没有独立自由语速滑杆或Live2D绑定模型。
- 清空/删除、安装到真实目录、云端预缓存等入口已登记，本次没有对真实用户执行这些操作。

工具白名单：`system.health`、`daily.summary`、`todo.list`、`todo.create`、`focus.start`、`files.preview`、`clipboard.review`、`document.summary`、`screen.read`。所有调用均经过 ToolHub，领域操作再复用原服务。聊天中的本地小脑单指令直接调用相同领域服务，不产生第二套计时器/数据库/播放器。
