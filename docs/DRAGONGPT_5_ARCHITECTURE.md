# DragonGPT 5.0 架构审查与增量实现

审查时间：2026-11-01。范围为当前 checkout。引用方案是需求依据，不作为测试证据。

## 编码前审查

| 模块 | 真实入口 | 初始分类与判断 |
|---|---|---|
| Qt6/C++20、Windows | CMakeLists.txt、src/main.cpp | A：已构建的28个测试中26个通过；2个失败见报告 |
| 角色与动画 | PetWidget、SpriteMotion、PetActions | A：已有七种动作、混合过渡、中断恢复、情绪映射；special_blinks测试通过。使用原生精灵，没有Cubism/Live2D模型 |
| 统一成长 | LifeState、MemoryStore::awardInteraction、FocusService | B：0–100进度、多日门槛、每日20 XP、冷却、对话去重、图鉴405条已存在；缺性格、任务成长与成长事件解释 |
| 情绪与音色 | EmotionEngine、VoiceController | A/C：已有优先级、到期恢复、所选音色链路；基线emotion_ui一个专注完成提示断言失败，需定位 |
| 主动生活 | Companion单次定时器、CompanionLife | B：冷却、日上限、专注/会议/全屏保护及忽略退避已有；缺每小时上限、习惯适配、决策原因 |
| 长期记忆 | MemoryStore、SemanticMemory | A/B：确认、来源、标签、去重、检索、修改、删除、导入导出已有；缺共同经历与成长层 |
| Agent与工具 | AgentCore、ToolHub、agent/worker.py | A/C：逐操作审批、会话工作区、取消、超时和审计已有；基线agent测试进程失败，需复查 |
| 剪贴板/监测/番茄钟 | ClipboardHistory、HealthMonitor、FocusService | A：对应基线测试通过；沿用采样与提醒，不新增截图或活动采集器 |
| 养成中心 | CompanionWindow | D：没有统一生活看板 |

A表示本轮对应测试通过，不代表云端、真人麦克风或长时间运行已验证。B为已有待升级；C为实际观察到失败；D为缺失。

## 实现约束

- GrowthProfile从属于现有LifeState，使用同一个SQLite连接/JSON记录；不另建亲密度、记忆服务或定时器。
- 角色文件、渲染参数、音色配置、凭据不由养成系统修改。现有PetActions及PetWidget继续负责互斥、优先级与恢复。
- 性格只改变受控表达偏好、台词权重和兼容待机动作；不代表真实意识，不产生离开惩罚、索取承诺或情感勒索。
- 共同经历仅由用户确认写入；自动成长记录保存事件类别和原因，不保存聊天/任务正文。私有经历默认不进入环境台词。
- 探索复用现有Agent入口与审批；开关只允许提出探索建议，不授予目录、网络、终端或外发权限。
- 不引入锁或新轮询线程；仅成功成长、设置保存和实际主动行为时写入。

最终修改、迁移和验证证据见使用指南与测试报告。

## 最终模块复用与修改

| 组件 | 本次职责 |
|---|---|
| LifeState / 新GrowthProfile | 同一成长存档内保存开关、阶段门槛、信任、六维基线/偏移/锁定、限量事件、时段计数与共同经历 |
| MemoryStore | 同一连接事务中保存配置与成长；共同经历确认/更正/删除；图鉴门槛换算；schema 8迁移和升级前备份 |
| Companion | 原单次调度器增加小时限制、时段适配和可选建议；任务结算；性格/经历上下文；离线回忆与表达；缓存动作所需状态 |
| DialogueEngine | 继续使用405条目录和已有去重，六维倾向只调整合格台词权重，不绕过情绪或阶段 |
| PetWidget / PetActions | 沿用优先级、互斥和恢复；成长仅选择已有安全动作，不修改SpriteMotion和角色资源 |
| LocalTools | 已存在待办从未完成变为完成时发出事件；没有增加待办服务或提醒定时器 |
| AgentCore / ToolHub | 原审批、工作区、异步、超时与取消保留；只读探索工具白名单在模型目录及执行处双重校验；关闭探索即取消 |
| 新GrowthCenter / dragon_growth_ui | 原主窗口内的纯视图，复用Companion信号；可独立测试，不持有另一套成长状态或计时器 |
| EmotionEngine / VoiceController / FocusService | 源码未改，继续提供情绪、所选音色、动作/语音恢复、番茄钟和水提醒；通过现有回归核对 |

可复用层与页面层分开，不为了该需求引入全局事件总线、第二套Agent或第二个数据库。扩展点继续是现有信号、ToolHub工具注册与受控的GrowthProfile字段。
