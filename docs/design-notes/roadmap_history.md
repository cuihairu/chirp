# Chirp 路线图历史(存档)

> 2026-09 自 TODO.md 归档。本文件是早期路线图与更新日志的历史记录;勾选状态反映归档时点的仓库状态,并已与 [../CAPABILITY_MATRIX.md](../CAPABILITY_MATRIX.md) 交叉核对。当前工作请看 [TODO.md](../../TODO.md)。

## 1. 基础设施(Monorepo 与 C++)

> 注：以下第 1–6 节为历史 roadmap，勾选状态已于 2026-09 按 [能力矩阵](../CAPABILITY_MATRIX.md) 对齐。`[x]` 仅表示代码存在；未标注的为核心 Supported 路径，标注 **Experimental** / **Demo** / **Stub** 的不应视为稳定能力。

- [x] **项目脚手架**:搭建 `libs`、`services`、`proto` 目录结构。
- [x] **构建系统**:根 `CMakeLists.txt` 与各模块配置。
- [x] **Protobuf 定义**:`common`、`auth`、`gateway` schema。
- [x] **网络库**:`chirp_network`(ASIO 封装、Packet 解析)。
- [x] **公共库**:配置加载器、日志器(spdlog 封装)。

## 2. 后端服务(微服务)

- [x] **网关服务**(边缘)
  - [x] TCP 服务器(面向游戏客户端)。
  - [x] WebSocket 服务器(面向 Web/移动应用)。
  - [x] 会话管理(本地内存)。
  - [x] 会话管理(分布式,Redis)。

- [x] **认证服务**（basic token 流程 = Supported；以下为增强实现，**Experimental**，依赖 MySQL + libsodium 条件构建，且目前零单测 — 见 P2）
  - [x] 登录/登出逻辑,密码哈希(Argon2)
  - [x] Token 校验(JWT)
  - [x] 多设备冲突处理(踢线逻辑)
  - [x] 刷新 token 支持
  - [x] 限流(基于 Redis)
  - [x] 防爆破
  - [x] 用户注册,MySQL 持久化

- [x] **聊天服务**(核心)（basic 消息 = Supported；存储增强与分布式为 **Experimental**）
  - [x] 1v1 消息路由。
  - [x] 离线消息存储(Redis → MySQL 迁移管线) **(Experimental)**
  - [x] 历史消息检索,带分页
  - [x] 消息送达跟踪(ACK/NACK) **(Experimental)**
  - [x] 混合消息存储(Redis + MySQL) **(Experimental — 且增强构建缺失 server plane/push bridge 文件，见 P1)**
    - [x] 从 Redis 历史/离线队列导出 MySQL 兼容 SQL
    - [x] 归档脚本与后台 worker
  - [x] 分布式路由(`chirp_chat_distributed` 目标) **(Experimental)**

- [x] **社交与在线状态服务** **(Experimental — 未验证为核心路径，零单测)**
  - [x] 好友列表管理。
  - [x] 实时状态同步(在线/游戏中),经 Redis Pub/Sub。

## 3. 实时语音(WebRTC) **(Experimental — 环境依赖重，不在最小验证路径，零单测)**

> 目标:面向游戏与移动应用的低延迟组队语音。

- [x] **信令服务**
  - [x] 房间管理(创建/加入/退出)。
  - [x] SDP 与 ICE candidate 交换。
- [x] **语音客户端模块**
  - [x] WebRTC 原生集成(C++)。
  - [x] 音频设备管理(麦克风/扬声器)。
  - [x] 网络自适应(抖动缓冲、FEC 配置)。

## 4. SDK(`sdks/`) **(Experimental — 不应宣称稳定)**

- [x] **核心 SDK(C++)** (`sdks/core`) **(Experimental)**
  - [x] 聊天与语音的统一接口。
  - [x] 跨平台编译(Windows/Mac/iOS/Android)。
- [x] **Unity SDK** (`sdks/unity` - C# 适配层) **(Experimental — social 绑定含 TODO)**
  - [x] C ABI 桥(原生插件)
  - [x] C# 封装(P/Invoke)
  - [x] MonoBehaviour 组件
  - [ ] 全功能的 Blueprint 函数 **(social 部分为 TODO 占位)**
- [x] **Unreal SDK** (`sdks/unreal` - UPlugin) **(Experimental — 存在未实现的返回路径)**
  - [x] UPlugin 描述符
  - [x] Blueprint 函数库
  - [ ] C++ 封装实现 **(部分函数为未实现的返回路径)**
  - [x] 事件派发器

## 5. 客户端应用(`apps/`)

- [x] **移动伴侣应用** (`apps/mobile_companion` - Flutter/RN) **(Demo — 不应呈现为生产就绪)**
  - [x] **UI 框架**:联系人列表、聊天窗口、语音房间。
  - [ ] **后台服务**:推送通知(APNs/FCM),保证应用被杀后用户仍"可达"。 **(provider HTTP 投递仍为日志 stub，见 P2)**
  - [x] **语音交互**:CallKit/ConnectionService 集成(系统电话 UI)。
- [x] **CLI 客户端** (`apps/cli_client` - C++) **(Demo — 用于 smoke/手工验证)**
  - [x] 简单的终端聊天客户端,用于测试。
- [x] **压测工具** (`apps/load_tester` - C++) **(Demo)**
  - [x] 连接洪泛测试
  - [x] 消息风暴测试
  - [x] 统计上报
- [ ] **管理后台** (`apps/admin_dashboard`) **(Stub — 使用 mock 数据与演示页面，未接真实后端)**

## 6. 运维与测试

- [x] **部署**:全部服务的 Dockerfile。 **(Supported)**
- [x] **编排**:Docker Compose / K8s 清单。 **(Docker Compose = Supported；K8s 为草案)**
- [ ] **压测**:网关 10k+ 并发连接基准。 **(容量数字需实测证据后方可对外宣称 — 见 architecture.md)**

## 近期更新(2026-03-18)

### 移动伴侣应用(Flutter)
- 新增带 WebRTC 集成的语音房间界面
- 新增带发言指示的参与者列表
- 新增静音/取消静音控制
- 新增扬声器切换
- 集成 ChirpClient 承担语音信令
- 首页增加语音房间入口

### Android 原生集成
- 创建带原生 method channel 的 MainActivity.kt
- 实现后台通话的 VoiceForegroundService
- 实现响应通知动作的 VoiceBroadcastReceiver
- 实现来电 UI 的 VoiceCallNotification
- 实现推送通知的 ChirpFirebaseMessagingService
- 创建含全部所需权限的 AndroidManifest.xml
- 添加带 Firebase 与 Kotlin 依赖的 build.gradle

### iOS 原生集成
- 创建带 CallKit 集成的 AppDelegate.swift
- 实现 VoIP 推送的 PKPushRegistryDelegate
- 实现通话管理的 CXProviderDelegate
- 添加音频焦点与免提控制
- 创建含麦克风/相机权限的 Info.plist
- 创建带 Firebase 与 CallKit 依赖的 Podfile

### 跨平台构建脚本
- 创建 macOS/Linux 构建的 build_all.sh
- 创建 Windows 构建的 build_all.bat
- 创建 iOS.cmake 工具链文件
- 创建 linux-gcc.cmake 工具链文件
- 创建全平台 Unity 插件构建脚本
- 增加 iOS XCFramework 制作

### WebRTC 原生客户端模块
- 创建 WebRTC 抽象层 webrtc_client.h
- 创建含音频设备管理的 webrtc_client.cc
- 将 WebRTC 客户端接入语音模块
- 增加音量监测
- 增加设备枚举 API
- 增加输入/输出启用/禁用控制

### 认证服务增强
- 新增 `refresh_tokens` 表,刷新 token 持久化
- 新增 `failed_login_attempts` 表,爆破追踪
- 新增 `password_reset_tokens` 表,密码找回
- 实现 Argon2id 的 `PasswordHasher`
- 实现安全 token 生成的 `TokenGenerator`
- 实现 MySQL 用户 CRUD 的 `UserStore`
- 实现多设备会话追踪的 `SessionStore`
- 实现分布式会话的 `RedisAuthStore`
- 实现 `RateLimiter` 与 `BruteForceProtector`
- 完整认证服务:注册、密码登录、刷新 token

### 聊天服务增强
- 创建集中配置的 `MessageStoreConfig`
- protobuf 扩展 ACK/NACK 与分页消息
- `MySQLMessageStore` 增加批量操作
- 创建 Redis + MySQL 双写的 `HybridMessageStore` 接口
- 创建 ACK/NACK 跟踪的 `MessageDeliveryTracker`
- 创建后台 Redis→MySQL 迁移的 `MessageMigrationWorker`
- 创建跨源分页的 `PaginatedHistoryRetriever`

### Unity SDK
- 创建 C ABI 桥(`chirp_unity_bridge.h/cc`)
- 创建 P/Invoke 的 C# 封装(`ChirpSDK.cs`)
- 创建 MonoBehaviour 组件(`ChirpManager.cs`)
- 原生插件构建配置(`CMakeLists.txt`)
- 支持 Windows、macOS、Linux、iOS、Android

### Unreal SDK
- 创建 UPlugin 描述符(`ChirpSDK.uplugin`)
- 创建覆盖全部聊天、社交、语音功能的 Blueprint 函数库
- 创建 C++ 封装实现(`ChirpClient.h/cpp`)
- 创建模块配置的 Build.cs
- Blueprint 事件派发器

### 压测工具
- 在 `apps/load_tester/` 创建 `LoadTester` 框架
- 连接洪泛场景(爬升到 N 连接)
- 消息风暴场景(高吞吐)
- 混合负载场景(贴近真实使用)
- 带延迟指标的统计上报

## 近期更新(2026-03-18 续)

### Discord 式频道系统
- 创建层级频道管理的 `channel_manager.h/cc`
- 用于组织的频道分类
- 文本/语音/公告/舞台/论坛频道类型
- 带角色/用户覆盖的权限系统
- 频道慢速模式支持
- 语音频道参与者管理

### 消息回应
- 创建表情回应的 `reaction_manager.h/cc`
- 带聚合的添加/移除回应
- 多消息的批量回应获取
- 供 UI 展示的热门回应

### 消息编辑/删除
- 创建消息编辑的 `message_edit_manager.h/cc`
- 可配置的编辑时间窗(默认 15 分钟)
- 带上限的编辑历史追踪
- 带保留期的软删除
- 管理员的硬删除
- 批量删除操作

### 正在输入提示
- 创建实时输入反馈的 `typing_manager.h/cc`
- 向频道参与者广播输入状态
- 可配置超时(默认 10 秒)
- 防止过度广播的冷却

### 提及系统
- 创建 @提及解析的 `mention_manager.h/cc`
- 支持 @user、@role、@channel、@everyone、@here
- 大规模提及的冷却
- 通知接收者构建
- 自动补全支持钩子

### 文件分享
- 创建文件上传/下载的 `file_storage_manager.h/cc`
- 预签名 URL 的上传预备
- 文件类型校验(图片、视频、音频、文档)
- 可配置大小上限(默认 100MB)
- 下载 URL 生成
- 病毒扫描钩子

### Prometheus 指标
- 创建指标采集的 `metrics.h/cc`
- Counter、Gauge、Histogram 指标类型
- /metrics 端点的 HTTP 服务器
- 健康检查端点
- 简单实现(无外部依赖)

### 移动端 CI/CD
- 创建 `.github/workflows/mobile-build.yml`
- Android APK/AppBundle 构建
- iOS 构建工作流
- Flutter 代码分析
- 原生 SDK 跨平台构建

## 近期更新(2026-03-18 续二)

### 消息编辑/删除
- 创建消息编辑的 `message_edit_manager.h/cc`
- 可配置的编辑时间窗(默认 15 分钟)
- 带上限的编辑历史追踪
- 带保留期的软删除
- 管理员的硬删除
- 批量删除操作

### 正在输入提示
- 创建实时输入反馈的 `typing_manager.h/cc`
- 向频道参与者广播输入状态
- 可配置超时(默认 10 秒)
- 防止过度广播的冷却

### 提及系统
- 创建 @提及解析的 `mention_manager.h/cc`
- 支持 @user、@role、@channel、@everyone、@here
- 大规模提及的冷却
- 通知接收者构建
- 自动补全支持钩子

### 文件分享
- 创建文件上传/下载的 `file_storage_manager.h/cc`
- 预签名 URL 的上传预备
- 文件类型校验(图片、视频、音频、文档)
- 可配置大小上限(默认 100MB)
- 下载 URL 生成
- 病毒扫描钩子

### 推送通知服务
- 创建支持 FCM/APNs 的 `services/notification`
- 设备注册与 token 管理
- 消息、提及与来电通知
- 静默通知支持
- 角标数管理
- 防骚扰的通知冷却
- 不活跃设备清理

### 在线状态增强
- 创建多设备在线状态的 `presence_manager_v2.h/cc`
- 状态类型:在线、闲置、勿扰、隐身、游戏中、语音中、通话中
- 带表情的自定义状态消息
- 活动检测与闲置/离线超时
- 按设备的会话管理
- 批量在线状态查询
- 在线好友检测

### 消息搜索服务
- 创建带全文索引的 `services/search`
- 快速文本搜索的倒排索引
- 按频道、发送者、时间范围、类型过滤
- 短语匹配与相关性打分
- 带上下文的高亮片段
- 查询建议/自动补全
- 搜索统计追踪

### Web 管理后台
- 创建 React 应用 `apps/admin_dashboard`
- 带图表的实时指标面板
- 带搜索与操作的用户管理
- 频道管理与监控
- 消息搜索界面
- 系统设置面板
- Material-UI 暗色主题
- Vite 快速开发
