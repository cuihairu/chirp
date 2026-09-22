# Chirp 文档

这个目录存放 VitePress 文档,包括当前运行时的说明、上手指南,以及一部分历史设计笔记。

## 先读这几篇

- [Core](./CORE.md):当前运行时契约的浓缩版事实来源
- [Capability Matrix(能力矩阵)](./CAPABILITY_MATRIX.md):按服务、SDK 和应用划分的现状
- [Introduction(简介)](./guide/introduction.md):给新读者的定位页,很短
- [Installation(安装)](./guide/installation.md):环境搭建与依赖说明
- [Getting Started(快速上手)](./guide/getting-started.md):构建、Docker Compose 与冒烟测试(smoke test)
- [API Overview(API 总览)](./api/overview.md):包封包格式、消息 ID 与核心流程
- [Overall Architecture(总体架构)](./architecture.md):拓扑、服务边界与架构评审结论

## 页面地图

- `Core docs`(本目录 + `guide/` + `api/`):受支持的运行时路径与协议契约
- `design-notes/`:历史性/设计性文章——部署、可扩展性、游戏聊天、战斗、NPC 对话、集成测试笔记等;仅作参考保留,**不**作为现行契约维护
- `Redirect pages`:`API.md` 和 `QUICKSTART.md` 指向受维护的页面,保留只为兼容旧链接
- `server_plane.md`:server plane(游戏后端 <-> chirp)的集成契约

## 当前定位

Chirp 目前比较准确的描述是:

- 一条受支持的后端主干:`gateway + auth + chat`
- 一个试验场:分布式聊天、更完整的认证、语音、社交、搜索、通知与 SDK 相关工作都在这里试
- 移动端和管理端的演示/占位仓库

请不要把这个仓库里的每个服务、SDK 或应用都当成稳定能力来介绍。现状以 [Capability Matrix](./CAPABILITY_MATRIX.md) 为准。

## 文档写作规则

新增或更新文档时,每个能力都要标注以下四种状态之一:

- `Supported`:属于默认文档化的后端主干,必须保持可构建
- `Experimental`:代码存在,但路径是有条件的、替代性的,或不属于最小验证运行时
- `Demo`:适合本地探索,不是稳定的后端契约
- `Stub`:未完成或由 mock 驱动

能更新 [Core](./CORE.md)、[Getting Started](./guide/getting-started.md)、[API Overview](./api/overview.md) 或 [Overall Architecture](./architecture.md),就不要再另起一篇重叠的总览。
