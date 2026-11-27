# Chirp 桌面端界面审核（impeccable audit）

- 日期：2026-11-21
- 目标：`apps/desktop`（Tauri v2 + React 19 + MUI 9 + Emotion，Operate 模式）
- 范围：登录页、主窗口（顶栏 + 左栏 + 聊天面板）、左栏各入口、6 个对话框、1024 宽窄屏
- 证据：`docs/review/desktop-audit/` 内 19 张实机截图 + 本次改动前的运行时取色记录
- 方法：本地起 `apps/desktop/dev/stack-up.sh` 全栈（auth+chat+gateway+social+party+voice，scaffold 登录），
  双账号 `uia`/`uib` 走真实会话（加好友→私聊收发→回复引用→建群→搜索→各入口对话框），
  Playwright Chromium 驱动截图；同一轮内用 `getComputedStyle` 取正文/次级/禁用三档文本色与有效底色算 WCAG 对比度。

## 环境限制（诚实声明）

Tauri 在 Linux 走 WebKitGTK、在 macOS 走 WKWebView，两者都不暴露 CDP，因此**没有**对原生外壳做
像素级读色；本次渲染取色与截图是在 **Chromium** 里对**同一份前端源码**跑的（Vite dev server）。
源码层结论（主题令牌、组件语义、键盘/焦点路径）与外壳无关；仅「原生窗口内滚动条、系统字体回退」
这类浏览器表面差异未验证。

## 审计健康分

| # | 维度 | 分 | 关键发现 |
|---|---|---|---|
| 1 | 可访问性 | 3 | 次级/禁用文本被解析成纯白，层级靠字号硬撑（已修）；2 处输入框只有 placeholder |
| 2 | 性能 | 2 | 消息行未 memo，任意 store 变化整列表重算；表情 Popover 常驻挂载 |
| 3 | 主题 | 2 | 硬编码 hex 绕过令牌；主题未声明 text 三级 |
| 4 | 响应式 | 3 | 左栏固定 300px；1024 宽无溢出；小尺寸图标按钮 |
| 5 | 实现完整性 | 4 | 产品特有一致（品质阶梯/坐标卡片/实验平面降级）；detector 0 findings |
| **合计** | | **14/20** | **Good** |

## 实现完整性判定

**通过。** 界面表达的是"游戏好友的聊天"这一具体产品，不是可替换的通用聊天壳：消息气泡按
`sender_kind` 分类上色、物品按品质白/绿/蓝/紫/橙阶梯、坐标卡片可点复制、组队/语音/好友上线
三个实验平面各自静默降级并把降级态写进左栏文案。机械检测器对 `apps/desktop/src` 扫描 **0 findings**。

## 截图索引

| 图 | 内容 |
|---|---|
| `01-login` / `02-login-validation` | 登录页、空用户 ID 校验错误态 |
| `03-main-empty` | 登录后主窗口空态（无会话、好友面降级态） |
| `04-friend-request` | 好友请求待接受 |
| `05-chat-private` / `06-chat-private-peer` | 私聊会话流（双端视角） |
| `07-typing-indicator` | 对端正在输入提示 |
| `08-reply-bar` / `09-reply-thread` | 引用回复输入条、引用块渲染 |
| `10-dialog-create-group` / `11-group-created` | 建群对话框、建群后群会话 |
| `12-dialog-group-manage` | 群管理（含邀请） |
| `14-dialog-search-empty` / `15-dialog-search-results` | 消息搜索空态与命中 |
| `16-dialog-party` / `17-dialog-voice` / `18-dialog-devices` | 组队、语音房、我的在线设备 |
| `19-narrow-1024` | 1024×640 窄窗口 |

## 详细发现（按严重度）

### P0

无。未发现阻塞性缺陷。

### P1

1. **文本层级被压平（已当场修，根因＝MUI 9 迁移坑）**
   - 位置：`src/theme.ts` + 6 个 UI 文件里 20 处 `color="text.secondary"`/`color="text.disabled"` 写法
   - 现象：`getComputedStyle` 在真实渲染页上取色，`color="text.secondary"`（顶栏用户 ID、左栏发送者名）
     与 `color="text.disabled"`（消息时间戳、左栏脚注）都返回与正文相同的 `text.primary` 纯色，
     有效底色 `rgb(31,34,42)` 上三者对比度同为 15.9:1。
   - 根因：MUI 9 的 `Typography color` 只认 `textSecondary`/`textDisabled` **驼峰**写法
     （类型联合是 `` `text${Capitalize<keyof TypeText>}` ``）；v5 时代的点号写法被类型上的
     `string & {}` 逃生口静默放行，运行时不生成任何颜色类，回落继承 `text.primary`。
     全仓 20 处点号写法自 MUI 9 迁移起就全部失效（与既有记忆「MUI9 props→sx」同类坑）。
   - 影响：WCAG 1.4.3 仍通过，但层级语义失效——时间戳、脚注这些低优先信息与正文同亮，
     弱视/低对比场景下无法用亮度区分主次。
   - 修复：20 处 `color="text.secondary|disabled|primary"` 批量替换为 `textSecondary`/`textDisabled`/
     `textPrimary` 驼峰写法（`sed` 全仓替换，残留 0）；主题显式声明 `text` 三档
     （`#eef1f4` / 70% / 50%），次级与禁用两档在 paper 上仍 ≥5:1。
   - 复测：修后取色 `text.primary=rgb(238,241,244)` 14.03:1、
     `text.secondary=rgba(238,241,244,0.7)` 7.55:1、
     `text.disabled=rgba(238,241,244,0.5)` 4.56:1——三档拉开，全部 ≥AA。
   - 波及提示：`web_companion` 与桌面端共源同款写法，大概率同病，需另开一次同步排查（未在本审计内改）。

2. **持续滚动内容缺静止选项（已当场修）**
   - 位置：`src/ui/MarqueeStrip.tsx`
   - 现象：走马灯 `18s linear infinite` 无限滚动，全仓 `grep prefers-reduced-motion` 为 0 命中，
     且探针确认 `cssMedia=false`（无任何 reduced-motion 媒体查询）。
   - 影响：WCAG 2.2.2（暂停、隐藏、隐藏移动内容）——超过 5 秒的自动移动内容必须可停。
   - 修复：`@media (prefers-reduced-motion: reduce)` 下 `animation: none`，同时去掉 `pl: 100%`
     （否则静止时内容停在视口外）。

### P2

3. **两处输入框无标签**
   - 位置：`src/ui/Rail.tsx` 加好友 `TextField`（仅 `placeholder="用户 ID"`，
     实测 `hasLabel=false` 且 `hasAriaLabel=false`）；`src/ui/ChatPanel.tsx` 消息输入 `InputBase`
     （仅 placeholder）。
   - 影响：屏幕阅读器只念出 placeholder，且部分平台聚焦后 placeholder 消失即无名称。
   - 处置：加好友输入已补 `aria-label="好友的用户 ID"`；消息输入已补 `aria-label="发送给 <会话名> 的消息"`。

4. **硬编码颜色绕过令牌**
   - 位置：`src/ui/ChatPanel.tsx` —— 自己的气泡文字 `#08130f`、失败描边 `#e57373`、
     `QUALITY_COLORS`/`RARITY_COLORS` 六组 hex。
   - 影响：主题切换/换肤时这些面不会跟随；同色值在两处复制。
   - 建议：抽成主题令牌（`palette.quality` 或 `shape` 旁的常量表），本次未动——改动面大且会牵动
     `web_companion` 的同款口径（两处刻意保持同文），需要单独一次同步。

### P3

5. **对话框主按钮文案不统一**：`SearchDialog` 用「取消」，其余 Shell 系对话框用「关闭」。
   证据：`SearchDialog.tsx:164` vs `Dialogs.tsx:45`。建议统一为「关闭」+「取消」按用途分区，或全部「关闭」。

6. **图标按钮偏小**：回复/举报/取消回复 `IconButton size="small"` + `p: 0.25`，实际边长约 26px，
   低于 WCAG 2.5.8 的 24×24 CSS px 下限附近（实测取到的 reply 按钮尺寸见 `sizes.json`）。
   桌面键盘用户无碍，触屏平板可用性问题。

7. **消息行未 memo**：`MessageRow` 非 `React.memo`，任一 store（未读、在线、presence）变化都会
   重算整条消息列表。会话长列表时是可观的浪费。建议 memo + 稳定化回调。

8. **表情 Popover 常驻**：`Popover open={emojiAnchor !== null}` 始终挂载（未用 `keepMounted=false` 之外的
   条件渲染），列表内每条 emoji 按钮都在 DOM 里。低优先，但可顺手改成 `open` 时才渲染内容。

9. **原生 `window.confirm` 做举报二次确认**（`ChatPanel.tsx:783`）：在 Tauri 里弹出的是原生确认框，
   与自研视觉语言脱节，且各 Webview 后端行为不一致。建议换成应用内确认弹窗（可复用现有 Shell）。

## 正面发现

- **降级态有文案**：social/party/voice 三平面任一缺席，左栏直接把降级状态写成可读句子
  （"social 平面未连接，好友面暂不可用"、"party 平面未连接"），不是灰掉一个图标了事——
  这是很多聊天客户端都会漏掉的用户体面。
- **空态齐全**：无会话、无好友请求、无好友、无匹配搜索，各有各自的引导句（`03-main-empty`、
  `14-dialog-search-empty`）。
- **图标按钮全有 `aria-label`**：回复、举报、取消回复、表情、发送五处均为文本图标按钮，无裸图标。
- **表单校验前置**：登录页两个必填项在本地就拦下（`02-login-validation` 是真实错误态截图），
  错误文案点名问题（"请填写用户 ID"），不是"输入有误"。
- **富文本口径一致**：`**粗体**`/`*斜体*`/`[emoji:code]` 与 `web_companion` 同源同文，
  未知 code 原样保留（向前兼容）。

## 建议后续命令

1. **[P2] `$impeccable polish`**：把 `ChatPanel` 的硬编码 hex 收进令牌，并同步 `web_companion` 同款口径。
2. **[P3] `$impeccable polish`**：对话框主按钮文案统一。
3. **[P3] `$impeccable optimize`**：`MessageRow` memo + 表情面板条件挂载。

修复 1/2/3（P1 两条 + 输入标签）已当场落地并通过桌面端 typecheck 与 62 例单测。

## 后续命令落地记录（2026-11-27 ✅ `36fd02b`）

1. **[P2] hex 收令牌（建议 1 + 第 4 条）**：自己气泡文字 `#08130f` 改引 `primary.contrastText`
   （desktop 主题显式钉值，换主色时主题一处跟随，与 web_companion 既有的同款令牌口径对齐）；
   失败描边 `#e57373` 改挂 `error.main`（与全 app 既有 `color="error"` 语义面一致，实色随主题
   落到 dark 缺省 `#f44336`）；品质/稀有度两组色表抽到 `apps/desktop/src/ui/rich_colors.ts`，
   web_companion 同步抽 `apps/web_companion/src/components/rich_colors.ts` 同文模块
   （MessageBubble 改 import，两 app 不共享 UI 代码、同文约定不变）。
2. **[P3] 按钮文案（建议 2 + 第 5 条）**：`SearchDialog` 底部「取消」→「关闭」（纯关闭用途，
   与 Shell 系口径一致）；`Dialogs.tsx` 内既有「取消」属动作确认流的取消，按用途分区保留。
3. **[P3] memo（建议 3 前半 + 第 7 条）**：`MessageRow` 包 `React.memo`；`onReply`/`onReport`
   改为带 messageId 的稳定 `useCallback`（经 `listRef` 解析当前消息，引用不随渲染重建），
   引用块展示面按父消息 id `useMemo` 缓存（依赖 list/channel/selfId/api）——三个不稳定引用
   全部钉住后 memo 才真正生效。stores 里未被改动的消息对象引用本就稳定（更新是
   `list.map` 按 id 替换），无关 store 变化（未读/在线/presence）不再重算整条列表。
   **建议 3 后半「表情 Popover 常驻」为误报，不动**：MUI Popover 基于 Modal，`keepMounted`
   默认 false（`Modal.d.ts` `@default false` 实测核对），关闭时 children 已卸载，不存在
   「列表内每条 emoji 按钮都在 DOM 里」，无需再加条件渲染。
4. **[P3] window.confirm（第 9 条）**：举报二次确认改为应用内 Dialog（标题「举报消息」，
   取消/举报两键，确认键 `data-testid="report-confirm"`、`color="error"`），视觉与自研
   对话框一致，Webview 行为不再因平台而异。
5. **[P3] 图标按钮 26px（第 6 条）**：WCAG 2.5.8 目标下限为 24×24 CSS px，实测 26px 已
   达标，触屏平板属增强项，不动。

门禁：desktop typecheck + vitest 62/62 绿；web_companion typecheck + eslint 绿 +
vitest 363/363 绿。
