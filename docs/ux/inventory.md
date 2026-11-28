# Chirp App 界面功能清单（CHIRP-UX-001 步骤一）

> 日期：2026-11-28 · 基准：main `4ed6495` · 方法：从代码反推，不虚构能力。
> 范围：6 个图形端（android / ios / harmony / desktop / web_companion / admin_dashboard）；
> `cli_client`、`load_tester` 为非图形端，不入清单。
> 本清单是步骤二（专业重设计）的输入：只记「有什么、在哪、完成度、端差异」，
> 设计裁决在 design-spec.md。

## 0. 关键既有设计资产（改变任务面的发现）

盘点后发现**设计语言已经存在且经过用户审核**，重设计不是从零起步：

| 资产 | 内容 | 状态 |
| --- | --- | --- |
| `design/prototypes/mobile/`（5 页 HTML + `chirp.css` + 6 张截图） | 手机通用稿：会话列表/聊天窗/好友/通知/我的；近黑底 `#0f1115` + indigo 单 accent `#6366f1` + 胶囊按钮 + 圆形头像；底部五页签=桌面 Rail 同源映射；🔔 通知与「我的」不占页签 | **用户一审通过**（01/02/04/05，2026-10-07）；03 好友页修订稿**代决通过**（FW1016D，2026-10-16，用户可推翻） |
| `design/prototypes/desktop/chirp-desktop-mobile-proto.html` | 桌面高保真原型（含手机端同风格重设计）；token 与移动稿同源；**明确把桌面现状的青绿 `#2ecc9f` 收敛到品牌 indigo**（对齐 logo/docs 站/web_companion 主色） | 设计稿（审核状态见该文件头部说明） |
| `docs/design-notes/app_chat_prototype.md` §三 | 手机端 ASCII 线框（其余未出 HTML 稿的屏以此为准） | 文档 |
| `apps/harmony/.../common/Tokens.ts` | **上述 chirp.css 的 ArkTS 映射**（近黑底 + indigo + 胶囊 + 圆头像，圆角/尺寸档位齐）——设计语言的第一个落地实现 | 已实现（鸿蒙端全量引用） |

**含义**：步骤二的工作重心不是「发明设计语言」，而是 (a) 把已批准的 token 体系整理成
跨端规范；(b) 为原型未覆盖的屏（登录/建群/组队语音/设备/群设置）按同 token 补稿；
(c) 逐端逐屏出差异表。品牌 indigo 与用户资产红线（logo 不动）均已在原型中确立。

## 1. 通用功能域（从协议面反推，各端共享的领域功能）

| 功能域 | 具体能力（协议面已存在） |
| --- | --- |
| 登录会话 | 密码登录 / 脚手架 token 登录（测试）/ 会话顶号（同账号他端登录踢线）/ refresh token |
| 消息 | 私聊 + 群聊；富文本（`**粗体**`/`*斜体*`/`[emoji:code]`）；回复引用；编辑；撤回；离线发送队列；已读回执（私聊）；正在输入；历史分页；gzip 压缩（`supportsGzip`）；词过滤（replace/reject/record 三态，词库同步） |
| 消息卡片 | 物品/坐标/成就/技能/交易/公会招募/组队招募 七种富卡片 + sender_kind 气泡分色（user/system/npc/service）+ 表情回应（quick reactions） |
| 好友 | 按 ID 加好友 / 请求-同意-拒绝 / 删除 / 在线离线 presence / 游戏好友（来源游戏，数据面为 game_presence） |
| 群 | 建群（名称/描述/TTL 档位）/ 群成员管理 / 角色 / 解散 / 群昵称 |
| 组队+语音 | 组队平面（邀请/成员/解散/离房）+ 语音平面（进房/说话状态/静音）；ASR 转写可选 |
| 设备 | 在线设备列表 / 本机标记 / 重命名 / 顶号契约（踢线是终态）/ 推送注册（FCM / APNs——凭据外部前置） |
| 通知 | 系统公告（Marquee 走马灯 + SystemAnnounce）/ 会话未读角标 / 通知抑制规则（`shouldNotifyFor`：隐藏频道且非当前频道才弹） |
| 运维 | admin REST `/api/*`（stats/users/channels/messages/settings 契约，`server/admin_api.mjs` 应答） |

## 2. Android（`apps/android`，Kotlin）——优先端

**界面现状：单屏调试控制台，无任何设计语言。**

| 功能 | 入口/组件 | 完成度 |
| --- | --- | --- |
| 登录（用户 ID + 对端 ID 两输入框 + 登录按钮 + 状态行） | `activity_main.xml`：`user_input`/`peer_input`/`login_btn`/`status_view` | 可用（raw widget，默认主题） |
| 收发消息（聊天记录 `chat_view` 为可选中文本 TextView，`message_input`+`send_btn`） | 同上，单 Activity 顺序渲染 | 可用（无气泡/无分色/无富文本渲染/无卡片） |
| 系统通知（渠道 + 推送注册） | `ChirpNotifications.kt`、`PushTokenHub`/FCM `ChirpFirebaseService` | 协议完整，UI=系统通知 |
| 协议层 | `protocol/` 19 个 Kotlin 文件：ChatPipeline/OfflineSendQueue/Frame/Gzip/WordFilterSync/Scheduler/DeviceRegistrar… | **完整**（带单测） |

丑点归因：不是「设计得丑」，是**没有界面层**——`src/app/kotlin` 仅 528 行（含通知），
布局只有一个 75 行的线性 XML；会话列表/好友/群/组队/语音/设备六个面在 UI 上不存在。
Material 3 主题未引入。协议能力与 UI 呈现的落差是全仓最大的一处。

## 3. iOS（`apps/ios`，SwiftUI）——优先端

**界面现状：纯系统默认样式 SwiftUI，8 个视图，无设计 token。**

| 功能 | 入口/组件 | 完成度 |
| --- | --- | --- |
| 登录 | `LoginView` | 可用 |
| 根切换（登录↔会话列表 + 连接横幅 + toast 胶囊） | `RootView`（`connectionBanner` + `.ultraThinMaterial` toast） | 可用 |
| 会话列表 | `SessionsView`（NavigationStack + NavigationLink） | 可用 |
| 聊天 | `ChatView` | 可用 |
| 好友 | `FriendsView` | 可用 |
| 群设置 | `GroupSettingsView` | 可用 |
| 组队语音 | `PartyVoiceView` | 可用 |
| 设备 | `DevicesView` | 可用 |
| 协议层 | `Sources/ChirpProtocol/`（ChatConnection/MessageSpec/Gzip/WordFilter/DeviceRegistrar） | **完整**（登录已声明 `supportsGzip`，macOS runner 云端验证过 `1a78f8e`） |

丑点归因：功能面与鸿蒙端同构（五平面），但视觉全是系统默认（列表灰白、无 token、
无品牌 indigo、无胶囊/圆头像语言）；`RootView` 已有的 toast 胶囊是唯一与设计稿
同族的元素。待做=把已批准设计语言映射为 SwiftUI 主题（Color/字体/圆角/组件样式）。

## 4. Harmony（`apps/harmony`，ArkTS）——设计语言已落地，作参照实现

| 功能 | 页面（`entry/src/main/ets/pages/`） | 完成度 |
| --- | --- | --- |
| 服务器基址配置（反代 ws 地址，五平面拼 `/ws/*`） | `HostPage`（web 端无此页） | 完整 |
| 登录 | `LoginPage` | 完整 |
| 入口路由（会话恢复/顶号弹回） | `Index` | 完整 |
| 会话列表（含 🔔→通知） | `SessionsPage`（407 行） | 完整 |
| 聊天 | `ChatPage`（507 行） | 完整 |
| 好友 | `FriendsPage`（333 行） | 完整 |
| 群 | `GroupPage`（669 行） | 完整 |
| 组队+语音 | `PartyVoicePage`（532 行） | 完整 |
| 设备 | `DevicesPage`（282 行） | 完整 |
| 通知中心 | `NotificationsPage`（396 行） | 完整 |
| 我的（个人卡/设备/设置/退出） | `ProfilePage`（379 行） | 完整 |
| 设计 token | `common/Tokens.ts`（C 色板/R 圆角/S 尺寸，单一事实源）+ `widgets.ets` | **chirp.css 的 ArkTS 映射** |

端定位：移动端里唯一按已批准设计语言实现的端，是 android/ios 重设计的**同源参照**
（页面结构≈设计稿五页 + 补齐的群/组队语音/登录/基址页）。

## 5. Desktop（`apps/desktop`，Tauri + React MUI）——功能最全，主题偏离品牌色

| 功能 | 入口/组件 | 完成度 |
| --- | --- | --- |
| 登录页（本地校验+点名错误文案） | `ui/LoginPage.tsx` | 完整 |
| 左栏平面导航（会话/好友/好友请求/组队/语音/设备 + 平面降级文案） | `ui/Rail.tsx` | 完整 |
| 公告走马灯（prefers-reduced-motion 支持） | `ui/MarqueeStrip.tsx` | 完整 |
| 聊天主面板：历史分页/气泡分色/富文本/引用回复/编辑/撤回/已读/正在输入/离线队列/失败原因/七种卡片/表情面板/举报应用内弹窗 | `ui/ChatPanel.tsx`（~900 行）+ `rich_colors.ts` | **全仓最全**（2026-11-21 审计 14/20 Good；P1 修复+P2/P3 清账 `36fd02b`） |
| 群管理（成员/角色/TTL/解散）、组队、语音、设备、搜索、系统公告、建群 弹窗 | `ui/Dialogs.tsx`、`SearchDialog.tsx`、`SystemAnnounceDialog.tsx` | 完整 |
| 主题 | `theme.ts`：MUI9 dark，primary **青绿 `#2ecc9f`**（本轮已钉 contrastText） | 与品牌 indigo 不一致（桌面原型已裁决收敛到 indigo） |

## 6. Web companion（`apps/web_companion`，React MUI）——与桌面同构

| 功能 | 入口/组件 | 完成度 |
| --- | --- | --- |
| 登录页 / 聊天页 | `pages/LoginPage.tsx`、`pages/ChatPage.tsx` | 完整 |
| 会话列表（私聊/群聊分组+未读） | `components/ConversationList.tsx` | 完整 |
| 聊天窗（气泡/七卡片/快捷回应/悬停动作） | `ChatWindow.tsx`+`MessageBubble.tsx`+`MessageInput.tsx` | 完整 |
| 好友/群/组队/语音/设备/在线设备/搜索/系统公告 弹窗 | `FriendsDialog`/`GroupDialogs`/`PartyDialog`/`VoiceDialog`/`DevicesDialog`/`OnlineDevicesDialog`/`SearchDialog`/`SystemAnnounceDialog` | 完整 |
| 连接横幅 + 公告走马灯 | `ConnectionBanner.tsx`、`MarqueeStrip.tsx` | 完整 |
| 行为开关 4 个（桌面通知/上线通知/私聊限制/游戏在线状态） | 设置面（采集管线覆盖） | 完整 |
| 主题 | `theme.ts`：MUI9 dark，primary **indigo `#6366f1`** | 与品牌色一致（是移动原型的取色来源） |

## 7. Admin dashboard（`apps/admin_dashboard`，React MUI + recharts）

| 功能 | 入口/组件 | 完成度 |
| --- | --- | --- |
| 仪表盘（MetricCard + AreaChart 趋势 + 表格） | `pages/Dashboard.tsx` | 完整 |
| 用户 / 频道 / 消息 / 设置 | `pages/Users|Channels|Messages|Settings.tsx` | 完整 |
| 侧栏导航 + 路由 | `App.tsx`（BrowserRouter + Drawer nav） | 完整 |
| 数据契约 | `server/admin_api.mjs`（前端 vitest 钉死） | 完整 |

定位：运维工具面，非「丑」主诉对象；设计语言统一时顺带对齐（次优先级）。

## 8. 端差异矩阵（同一功能在各端的 UI 呈现）

| 功能 | android | ios | harmony | desktop | web | admin |
| --- | --- | --- | --- | --- | --- | --- |
| 登录 | 单输入行 | LoginView | LoginPage+HostPage | Login 页 | LoginPage | —（本地设置） |
| 会话列表 | ✗ | SessionsView | SessionsPage | Rail 会话区 | ConversationList | — |
| 聊天（气泡/富文本/卡片） | 纯 TextView ✗ | ChatView（基础） | ChatPage（token 化） | ChatPanel（最全） | ChatWindow | Messages 只读表 |
| 好友 | ✗ | FriendsView | FriendsPage | Rail+Dialogs | FriendsDialog | Users 表 |
| 群管理 | ✗ | GroupSettingsView | GroupPage | Dialogs | GroupDialogs | Channels 表 |
| 组队/语音 | ✗ | PartyVoiceView | PartyVoicePage | Rail+Dialogs | PartyDialog/VoiceDialog | — |
| 设备 | ✗ | DevicesView | DevicesPage | Dialogs | DevicesDialog/OnlineDevicesDialog | — |
| 通知中心 | 系统通知 | toast/banner | NotificationsPage | Marquee+Dialog | Marquee+Dialog | — |
| 我的/设置 | ✗ | — | ProfilePage | Rail 顶栏开关 | 开关（App 内） | Settings 页 |
| 设计语言 | 无 | 无 | **Tokens.ts（已批准语言）** | MUI 青绿（偏离） | MUI indigo（同族） | MUI 默认 |

## 9. 步骤二输入结论

1. **设计语言不新造**：以已批准的 `chirp.css` token 体系为唯一事实源整理
   `design-spec.md`；桌面原型已有的「青绿→indigo 收敛」裁决一并纳入。
2. **移动优先**（用户令）：android（从零建界面层，Material 3 + 设计 token）与
   ios（系统样式→token 化主题）是主战场；harmony 是参照实现（只对齐不重做）。
3. **原型缺口**：登录/建群/组队语音/设备/群设置五屏无 HTML 稿（ASCII 线框兜底），
   步骤二按同 token 补稿后随其余设计稿一并送用户过目。
4. 桌面/web/admin 的主题对齐为次优先级（功能已全，只换皮肤口径）。
