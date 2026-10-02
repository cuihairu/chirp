# Chirp Desktop App

Chirp 桌面聊天 App（App 平面桌面客户端）：Tauri v2 壳 + React/MUI，经 app_gateway WS 与自家 gateway+auth+chat 主线通信。

## 功能面
- 登录会话（scaffold：用户 ID 即凭据）
- 会话列表：私聊 / 群聊，未读角标、最近消息预览
- 消息收发：文本、已读回执、正在输入指示、离线消息自动重放
- 好友系统：加好友、请求列表、同意/拒绝、在线状态（绿点）
- 群聊管理：创建群、邀请/踢人、退群、群主标识
- 组队入口：创建/接受邀请、转让队长、解散/退出（接 party 平面）
- 语音房入口：创建/加入房间、静音/离开（接 voice 平面，信令闭环，媒体面未验证）
- 多端在线：本账号其他在线端清单（同型顶号提示）
- 桌面通知：新消息到达时弹出系统通知（需授权）

## 架构拓扑
```
桌面端 (Tauri v2 + React/MUI)
       │
       ▼ WS :5201
app_gateway (chirp_app_sdk_gateway)
   ├─→ auth (chirp_app_auth) :5300  scaffold login
   └─→ chat (chirp_chat) :5310/5311  消息/历史/已读/群/好友在线
       ├─→ social (chirp_social) :8001  好友/在线状态
       ├─→ party (chirp_party) :7501  组队
       └─→ voice (chirp_voice) :9001  语音信令
```
- 主连接走 App 平面边缘（gateway WS 5201），social/party/voice 为可降级平面——任一缺席或宕机时聊天主线照常，对应功能面隐藏
- 设备推送/游戏在线平面为移动端语义，桌面端不接

## 本地开发

### 1) 启动后端栈（隔离 redis + 自建 MariaDB）
```bash
cd apps/desktop/dev
./stack-up.sh
```
- 要求：本机 MariaDB 127.0.0.1:3306 已建 `chirp` 库（含全量表）
- 产物：ws://127.0.0.1:5201（桌面端登录地址，scaffold：用户 ID 即凭据）
- 关键端口：redis 6390、auth 5300、chat 5310/5311、gateway 5201/5320、social 8001、party 7501、voice 9001

### 2) 启动前端开发服务器
```bash
cd apps/desktop
npm run dev
```
- vite 监听 5180（5173 被占用）
- Tauri dev 模式：`npm run tauri dev`（同时启动 vite + cargo run）

### 3) 收尾
```bash
cd apps/desktop/dev
./stack-down.sh
```

## 打包发布

### 构建 .deb（Linux）
```bash
cd apps/desktop
npm run tauri build
```
产物：`src-tauri/target/release/bundle/deb/Chirp_0.1.0_amd64.deb` (约 4 MB)
依赖：`libwebkit2gtk-4.1-0`、`libgtk-3-0`

安装：
```bash
sudo dpkg -i src-tauri/target/release/bundle/deb/Chirp_0.1.0_amd64.deb
# 缺依赖时：
sudo apt-get install -f
```

### 运行打包后的 App
```bash
chirp-desktop
# 或应用菜单中找到 "Chirp"
```

## 关键配置文件
- `src-tauri/tauri.conf.json` — Tauri v2 配置（窗口尺寸、CSP、打包目标 deb、图标）
- `src-tauri/capabilities/default.json` — 权限：core:default + notification:default
- `vite.config.ts` — 别名 `@chirp/proto` → `../../proto/ts/proto`，`@chirp/app-protocol` → `../shared/protocol/src`，端口 5180
- `src/api/services.ts` — 服务图构建，平面连接与降级逻辑

## 核心类型与复用
- 直接复制（非深层依赖） web_companion 的 `src/state/*.ts` 与 `src/api/*.ts`
- `@chirp/proto/*` 与 `@chirp/app-protocol/*` 通过 tsconfig paths + vite alias 解析(app 不依赖 `sdks/`——那是游戏接入面,app 侧协议层在 `apps/shared/protocol`)
- `Store<T>` 基于 `useSyncExternalStore` 的最小响应式存储，配合 `useStoreValue` hook
- `ChirpClient` / `ChatApi` / `msg_map` 规范：见 `apps/shared/protocol/src/`

## Xvfb 无头走查（CI/本地验收）
```bash
# 启动 Xvfb + openbox
Xvfb :99 -screen 0 1600x1000x24 +extension RENDER +extension RANDR +extension XFIXES +extension DAMAGE +extension COMPOSITE +extension SHAPE +extension MIT-SHM -ac &
openbox --display :99 &

# 运行桌面端（需 WEBKIT 变量避免黑屏）
DISPLAY=:99 WEBKIT_DISABLE_COMPOSITING_MODE=1 WEBKIT_DISABLE_DMABUF_RENDERER=1 \
  ./src-tauri/target/release/chirp-desktop &

# 截图
ffmpeg -y -f x11grab -video_size 1600x1000 -i :99 -frames:v 1 out.png

# xdotool 驱动：先 mousemove 点击聚焦，再 type --delay 50 输入，最后 key Return 提交
```

## 故障排查
- **黑屏**：确保 `WEBKIT_DISABLE_COMPOSITING_MODE=1 WEBKIT_DISABLE_DMABUF_RENDERER=1`
- **窗口 10×10**：无窗口管理器时需 `xdotool windowsize <win> 1180 760`
- **输入不生效**：xdotool 需先 `mousemove X Y click 1` 聚焦再 `type`
- **devUrl 变更需重新 cargo build**：`generate_context!()` 在编译期烘焙 devUrl
- **平面连不上**：检查 `planeWsUrl` 是否正确剥离 gateway 端口（已修复）
- **social 不在栈里**：`stack-up.sh` 需包含 `chirp_social`（已修复）

## 目录结构
```
apps/desktop/
├── dev/
│   ├── stack-up.sh      # 启动隔离后端栈
│   └── stack-down.sh    # 收尾
├── src/
│   ├── api/
│   │   ├── services.ts      # 服务图、连接构建、平面 URL 解析
│   │   ├── chat_api.ts      # 聊天主功能（复用 web_companion）
│   │   ├── social_api.ts    # 好友/在线状态（复用）
│   │   ├── party_api.ts     # 组队（复用）
│   │   ├── voice_api.ts     # 语音信令（复用）
│   │   └── desktop_notify.ts # 桌面通知（Tauri + Web 双实现）
│   ├── state/               # 存储层（复用 web_companion）
│   ├── ui/                  # React 组件
│   │   ├── LoginPage.tsx
│   │   ├── ChatPanel.tsx
│   │   ├── Rail.tsx
│   │   ├── Dialogs.tsx      # 群管理/组队/语音/设备对话框
│   │   └── MainWindow.tsx
│   ├── theme.ts             # MUI 暗色主题
│   ├── App.tsx              # 登录状态机、顶号处理、平面登录、通知订阅
│   └── main.tsx
├── src-tauri/
│   ├── Cargo.toml
│   ├── tauri.conf.json
│   ├── capabilities/default.json
│   ├── icons/
│   └── src/
│       ├── main.rs
│       └── lib.rs
├── package.json
├── tsconfig.json
├── vite.config.ts
└── README.md (本文件)
```