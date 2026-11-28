# Chirp 跨端设计规范（CHIRP-UX-001 步骤二）

> 日期:2026-11-28 · 基准:main `353b36b` · 输入:[inventory.md](inventory.md)(步骤一功能清单)
> 状态:**设计稿,待用户过目——未过目不进实现**(步骤三实现须等本规范+原型确认)。
> 方法:设计语言不新造,以**用户已审核通过**的 `design/prototypes/mobile/chirp.css`
> token 体系为唯一事实源整理成文;原型缺口五屏按同 token 补稿
> (`design/prototypes/mobile/06–10`,同批送审);逐端逐屏差异表以步骤一清单为底。

## 1. 结论先行

1. **品牌色 = indigo `#6366f1` 系**(与 logo/docs 站/web_companion 同族);**深色为规范态**,
   全部现有实现均为深色,浅色为提案待拍板(§2.2)。
2. **移动优先**(用户令):android 从零建界面层、ios 系统样式→token 化是主战场;
   harmony 已是设计语言落地实现(Tokens.ts),作同源参照,只对齐不重做;
   desktop/web/admin 主题对齐为次优先级。
3. **桌面青绿 `#2ecc9f` 收敛到 indigo** 沿用桌面原型既有裁决
   (`design/prototypes/desktop/chirp-desktop-mobile-proto.html` 头部说明),本规范成文化。
4. **logo 是用户资产,不可替换**:各端实现只用品牌 logo 资产本身;原型中的几何声波记号
   仅为设计稿占位,不是新 logo 提案。
5. 语音媒体面(频谱/波形/音量条)不设计——音频媒体面未接入,信令名单为协议面现状。

## 2. 设计语言

### 2.1 色彩(深色=规范态)

| Token | 值 | 用途 | 对应实现命名(建议) |
| --- | --- | --- | --- |
| bg | `#0f1115` | 页面底 | android `background`;ios `bg`;ArkTS `C.bg`;MUI `background.default` |
| surface | `#161923` | 卡片/顶栏/输入条 | android `surface`;MUI `background.paper` |
| surface-2 | `#1c2030` | 次级面:气泡(他人)/输入域/分段底 | android `surfaceVariant`;ios `surface2` |
| surface-3 | `#1c2030`→`#232840` | 强调底(开关未开/回应 chip 底) | ios `surface3`;ArkTS `C.surface3` |
| border | `#2a3046` | 输入描边/ghost 按钮描边 | `outline` |
| border-soft | `#222741` | 卡片描边/分隔线 | `divider` |
| text | `#f3f4f6` | 主文本 | onSurface;`text.primary` |
| text-2 | `#9ca3af` | 次文本(预览/副标题) | onSurfaceVariant;`text.secondary` |
| text-3 | `#6b7280` | 弱文本(时间戳/注记) | `text.disabled` 档(实现端三档文本必须真实拉开,桌面 2026-11-21 审计 P1 教训) |
| accent | `#6366f1` | 主操作/未读角标/激活页签 | primary |
| accent-strong | `#4f46e5` | 自己气泡底/渐变深端 | primary 深档 |
| accent-soft | `#a5b4fc` | 深底上的强调文本/激活页签色 | primary 亮档(深底专用,**勿在浅底用**) |
| accent-dim | `rgba(99,102,241,.14)` | 回应 chip 底/筛选 chip 选中底 | primaryContainer |
| online | `#34d399` | presence/成功/「说话中/已准备」chip | success |
| danger | `#f87171` | 踢线警示/危险按钮/未读角标点 | error |
| warn | `#fbbf24` | 拒听/禁言等受限态 chip | warning |

- 文本对比度门禁:正文对底 ≥4.5:1;text-3 仅用于时间戳/注记级弱信息(实测 4.6:1)。
- **浅色模式 = 提案,待用户拍板,未实现任何端**:bg `#f5f6fa` / surface `#ffffff` /
  surface-2 `#eef0f6` / border `#d9dde8` / text `#111827`/`#4b5563`/`#9ca3af` /
  accent 不变 / online `#10b981` / danger `#ef4444` / warn `#d97706`。
  深浅两态都要过对比度门禁后才许实现。

### 2.2 字体排印(平台系统字体,不引外部字体文件)

| 档 | 规格 | 用途 |
| --- | --- | --- |
| display | 21px / 700 | 登录页标题(唯一一档) |
| title | 17px / 700 | 顶栏标题 |
| body | 15px / 400,行高 1.45 | 正文/气泡/列表名(名加 600) |
| secondary | 13px / 400 | 预览行/副标题/描述 |
| caption | 12px / 400 | 注记/提示 |
| label | 12px / 700,+0.06em 字距 | 分组标签(section-label)/表单字段标签 |
| meta | 10.5–11px / 400 | 时间戳/消息元数据 |
| chip | 11–11.5px / 600 | 状态 chip/页签文字(10.5/600) |

数字(时间戳/角标)用系统等宽变体不强制;汉字环境字体栈:
`system-ui, -apple-system, "Segoe UI", Roboto, "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Noto Sans CJK SC", sans-serif`。

### 2.3 间距、圆角、尺寸

- **间距阶**:4 为基步,常用 4/8/10/12/14/16;页面左右留白 16,内容纵向节奏 12;
  行内边距 12×14;卡片间距 12。
- **圆角阶**:卡片 16 / 行与输入域 12 / 胶囊(按钮/chip/分段)999 / 气泡 18(尾角 6)/
  头像全圆 / 品牌占位块 16。
- **尺寸档**:顶栏 56;底部页签 64;头像 44(列表)/32(密集行)/72(个人卡);
  图标按钮 36;圆形发送键 40;输入域高 40–44;开关 46×27(球 21);presence 点 12;
  未读角标最小 20;通知角标点 16。
- 安全区:底部页签与输入条吃 `safe-area-inset-bottom`(原型 CSS 已带,实现端按平台惯例)。

### 2.4 图形语言

- **图标**:线性笔触 SVG(1.8 stroke,圆帽圆角),24 viewBox,单色 currentColor;
  **禁 emoji 当图标、禁Unicode 符号当图标**(impeccable craft-floor 红线)。
- **头像**:圆形,底色按名字色相散列 `hsl(h,42%,46%)`,白字母;在线=绿点(presence,
  带 2.5px 底色描边);可选 2px accent 环(当前用户)。
- **logo 占位**:几何声波记号,仅设计稿内用;实现端放用户提供的真实 logo 资产。
- **动效**:原型仅静态;实现端克制——presence/开关 0.18s 过渡、列表无入场动画;
  走马灯类持续滚动内容必须带 `prefers-reduced-motion` 静止档(桌面审计 P1 已立此规)。

### 2.5 组件规范(与原型 CSS 一一对应)

| 组件 | 规格 | 状态 |
| --- | --- | --- |
| 顶栏 | 56 高,bg 92% 不透明度,下缘 1px border-soft;左返回/右图标按钮 36 圆 | 副标题位挂服务连接态/typing |
| 底部页签 | 64 高,五项=聊天/好友/组队/语音/设备;图标 22 + 10.5px 字;激活 accent-soft | 通知中心与「我的」**不占页签** |
| 胶囊按钮 | 高 ~37(padding 10×18),字号 14/600;primary=accent 白字/ghost=描边/danger-ghost=红字描边/sm 档 6×14 | 置灰=不可用;提交中换文案(「登录中…」「创建中…」) |
| 列表卡 | surface + 1px border-soft + 圆角 16,行间分隔线;行=头像+名/预览+右元数据 | 未读=accent 胶囊;presence 绿点 |
| 状态 chip | 11/600 胶囊;live=绿淡底/game=accent 淡底/warn=黄淡底/中性=surface-2/src=描边弱化 | 语义固定:已准备/说话中=live;群主/游戏=game;禁言/拒听=warn |
| 气泡 | 最大宽 72%,他人=surface-2 左下尾角 6,自己=accent-strong 右下尾角 6;内 9×13 | 自己元数据半透明白;撤回=系统行;回应 chip 挂下缘 |
| 表单字段 | 高 44,圆角 12,surface-2 底 + border 描边,**可见标签**(12/700)置于字段上方 | 错误=danger 描边+danger caption,行内报错 |
| 分段控制 | surface-2 圆胶囊容器 + 3px 内距,选中段 accent 白字 | 用于 TTL 档位/好友视图切换 |
| 开关 | 46×27,开=accent,球白 21 | 关=surface-3 |
| 全屏 sheet | 对话框一律全屏推入,顶栏带返回;底层页签半透明示意 | 建群/添加成员/成员操作/气泡操作 |

## 3. 屏幕清单与原型索引(共 10 页,`design/prototypes/mobile/`)

| 页 | 文件 | 状态 |
| --- | --- | --- |
| 01 会话列表(首页) | `01-sessions.html` | 用户一审通过(2026-10-07) |
| 02 聊天窗口 | `02-chat.html` | 用户一审通过(2026-10-07) |
| 03 好友/游戏好友 | `03-friends.html` | 修订稿代决通过(FW1016D,2026-10-16,可推翻) |
| 04 通知 | `04-notifications.html` | 用户一审通过(2026-10-07) |
| 05 我的 | `05-profile.html` | 用户一审通过(2026-10-07) |
| 06 登录 | `06-login.html` | **2026-11-28 补稿,待用户过目** |
| 07 新建群组 | `07-group-create.html` | **2026-11-28 补稿,待用户过目** |
| 08 组队语音 | `08-party-voice.html` | **2026-11-28 补稿,待用户过目** |
| 09 设备 | `09-devices.html` | **2026-11-28 补稿,待用户过目** |
| 10 群设置 | `10-group-settings.html` | **2026-11-28 补稿,待用户过目** |

每页布局/交互/状态说明见同目录 `README.md`;截图 `shots/*.png`(375×812 @2x)。

## 4. 逐端逐屏差异表(现状 → 目标)

量级:零=已达标;小=改样式;中=改结构;大=从零建。

### 4.1 Android(优先端,现状=无界面层)

| 屏 | 现状 | 目标 | 量级 |
| --- | --- | --- | --- |
| 登录 | 两个 raw EditText + 按钮 + 状态行(`activity_main.xml` 75 行线性布局) | 06 稿:品牌区+可见标签字段+行内校验+基址项 | 大 |
| 会话列表 | ✗ 不存在 | 01 稿:分组列表+未读+双主操作+五页签骨架 | 大 |
| 聊天 | 单个可选中文 TextView,无气泡 | 02 稿:气泡流+回应+已读/撤回/typing+输入条 | 大 |
| 好友 | ✗ | 03 稿(请求卡/来源标签/筛选) | 大 |
| 建群 | ✗ | 07 稿(sheet:名称/描述/TTL 档位/初始成员) | 大 |
| 群设置 | ✗ | 10 稿(成员/角色/禁言/退出/解散) | 大 |
| 组队语音 | ✗ | 08 稿(双平面卡+状态 chip+开关) | 大 |
| 设备 | ✗(协议层完整,UI=系统通知) | 09 稿(本设备/在线设备/契约注记/偏好) | 大 |
| 通知/我的 | 系统通知;✗ | 04/05 稿 | 大 |
| 主题 | Material 默认 | Material 3 + §2 token 映射(color scheme/shape/type 全量) | 大 |

实现口径:Material 3 组件承载(Cards/TopAppBar/NavigationBar/FilledTonalButton→按 §2.5
改样式),token 落 `colors.xml`/`themes.xml`/`dimens.xml` 单一事实源;协议层
`protocol/` 19 文件不动,只建 UI 层。

### 4.2 iOS(优先端,现状=系统默认样式 SwiftUI 8 视图)

| 屏 | 现状 | 目标 | 量级 |
| --- | --- | --- | --- |
| 登录 | LoginView 系统样式 | 06 稿 token 化 | 小 |
| 会话列表 | SessionsView 系统 List | 01 稿:分组卡列表+未读胶囊+双主操作 | 中 |
| 聊天 | ChatView 基础 | 02 稿:气泡/回应/元数据/输入条 | 中 |
| 好友 | FriendsView | 03 稿(含来源标签/筛选) | 中 |
| 群设置 | GroupSettingsView | 10 稿 | 小 |
| 组队语音 | PartyVoiceView | 08 稿 | 小 |
| 设备 | DevicesView | 09 稿 | 小 |
| 通知/我的 | toast 胶囊(唯一同族元素)+ 无 | 04/05 稿补齐;RootView 横幅/toost→token | 中 |
| 主题 | 无 token | §2 全量映射为 SwiftUI `Color`/`Font`/shape 资产 + 组件 ViewModifier | 中 |

实现口径:先建 Theme 资产(颜色/字阶/圆角),再逐屏替换;NavigationStack 结构保留。

### 4.3 Harmony(参照实现,已落地)

| 项 | 现状 | 目标 | 量级 |
| --- | --- | --- | --- |
| Token | `Tokens.ts` = chirp.css 映射(C/R/S 齐全) | 与本规范逐项核对命名与档位,补缺(如 warn/accent-dim 若缺) | 小 |
| 十一页 | 会话/聊天/好友/群/组队语音/设备/通知/我的/登录/基址/入口 全 token 化 | 与 06–10 新稿核对:登录基址并入口径、TTL 档位、状态 chip 语义是否一致;不一致处以本规范为准微调 | 小 |
| 主题差异 | 无 | 无(深色即规范态) | 零 |

### 4.4 Desktop(功能最全,主题偏离)

| 项 | 现状 | 目标 | 量级 |
| --- | --- | --- | --- |
| 主色 | 青绿 `#2ecc9f`(contrastText 已钉 `#08130f`) | **indigo `#6366f1`**(桌面原型既有裁决成文化);contrastText 相应改浅底深字 | 小 |
| 文本三档 | 已修(驼峰写法+显式三档,2026-11-21 P1) | 数值对齐 §2.1(text-3 4.6:1 口径) | 小 |
| 组件圆角 | MUI 默认 | 按 §2.3 圆角阶(shape.card 16/pill 999) | 小 |
| 布局 | 左栏 Rail+聊天面板(桌面形态,非移动单列) | **保留桌面布局**,只对齐色彩/字阶/圆角/按钮语言——桌面不做五页签 | 零(布局) |

### 4.5 Web companion(与桌面同构)

| 项 | 现状 | 目标 | 量级 |
| --- | --- | --- | --- |
| 主色 | indigo `#6366f1` | 不变(已是品牌色) | 零 |
| 文本/圆角/按钮 | 同桌面历史坑(部分已同步修) | 随桌面同批对齐 §2(圆角阶/三档文本数值) | 小 |

### 4.6 Admin dashboard(工具面,次优先级)

| 项 | 现状 | 目标 | 量级 |
| --- | --- | --- | --- |
| 主题 | MUI 默认蓝 | 换 §2 色板(indigo primary/深色底),布局不动 | 小 |

## 5. 实施顺序(步骤三,须等用户过目本规范+原型后开工)

1. **Android**:主题骨架(token 落地)→ 登录 → 会话列表+页签骨架 → 聊天 →
   好友 → 建群/群设置 → 组队语音 → 设备/通知/我的。每屏一个 commit,构建绿。
2. **iOS**:Theme 资产 → 逐屏替换(顺序同上,量级小先行)。
3. **桌面/web/admin**:一次主题对齐批(青绿→indigo + 圆角阶 + 三档文本),单 commit。
4. 每屏实现完成后:模拟器/真机截图 vs 原型 mockup 对照,随 commit 附;**UI 闭项 =
   用户真机目验**(用户令,验收口径)。
5. harmony 对齐核查(§4.3)穿插在移动两端之间做,不独立占主线。

## 6. 待用户裁决项(实现前须拍板)

| # | 事项 | 默认方案(不拍板则按默认) |
| --- | --- | --- |
| 1 | 06–10 五页补稿是否通过 | 不通过则按意见修订重发审 |
| 2 | 浅色模式提案(§2.1)是否要做 | 默认不做,深色即规范态 |
| 3 | TTL 自定义分钟输入档保留否(07 稿只出三档) | 默认三档(harmony 自由输入改为档位) |
| 4 | 登录页服务器基址项折叠口径(06 稿并入单屏) | 默认并入单屏、已配置后折叠(harmony 独立 HostPage 可保留实现,交互按单屏口径) |
| 5 | 桌面青绿→indigo 确认(既有裁决成文化) | 默认执行(桌面原型已裁决) |

## 7. 红线与口径(重申)

- logo 为用户资产,任何端不得替换/重绘;原型声波记号仅占位。
- 未实现的能力不写进设计稿;协议面现状如实注记(语音媒体面/推送厂商通道/好友来源字段)。
- commit 信息英文、无 AI 痕迹;每屏一 commit;测试绿才 push。
- 正式版本发布红线不变:版本号由用户手动给出并触发。
