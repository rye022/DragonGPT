# DeepSeek 连接

v1.2 使用官方 Responses 接口 https://api.deepseek.com/responses，默认 deepseek-flash。模型名按最新官方文档与账户权限填写。
选择直接连接无需 VPN；显式代理失败不改为直连。启动及待机不发送聊天；在线开关开启后发送消息才请求。
DEEPSEEK_API_KEY 优先，否则读取 data/deepseek-key.dpapi。OpenAI 环境变量和旧 api-key.dpapi 不会用于 DeepSeek。
只发送人格提示词、当轮消息、最近 20 条历史、称呼和最多 3 条相关记忆；上下文作为 JSON 数据。reasoning.effort=none，不朗读思考内容。
流式取消、401/429、超时、断流会明确显示状态；错误不自动重试。回退回复标为本地，成功回复标为 DeepSeek，旧历史保留原有来源。
本地规则始终处理“叫我…”与“记住：…”。API 用量只统计收到的 usage，历史服务记录仍在本机累计中；不能当作账户余额或聊天订阅额度。停止客户端不能保证服务端立即停止处理或计费。
本次没有真实 DeepSeek key，网络逻辑由回环模拟服务验证。

[DeepSeek 官方接口](https://api-docs.deepseek.com/api/create-response/) · [模型与首次调用](https://api-docs.deepseek.com/)
