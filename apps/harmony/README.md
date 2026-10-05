# Chirp 鸿蒙伴侣端（ArkTS，代码-only）

HarmonyOS NEXT（stage 模型）第三套原生壳，消费共享 TS 协议核。与 apps/android
（Kotlin）、apps/ios（Swift）同一功能面：登录、会话列表、单聊/群聊收发、已读回执、
输入中指示、表情回应、消息编辑/删除、好友、群组、设备（推送目标 + 多端在线 +
游戏在线开关）、组队、语音房（协议面，无媒体）。

## 分层

```
entry/src/main/ets/
  code/api/      .ts  API 层（web_companion api/ 的框架无关移植，React 面不搬）
  code/state/    .ts  stores（web_companion state/ 的逐文件移植，快照语义不变）
  common/        .ets 平台适配层（WebSocket 传输、地址持久化、设备指纹、通知）
  pages/         .ets ArkUI 页面（Index 守卫 + Host/Login/Sessions/Chat/
                      Friends/Group/Devices/PartyVoice）
```

依赖接线（与 apps/desktop 的 vite 别名同源单一事实源）：

- `@chirp/app-protocol` → `file:../../shared/protocol`（连接状态机、帧编解码、
  40 对 MsgID 表、钩子管线、词库下发同步；测试向量为 vitest 全绿的那一份，
  本端零复制零漂移）
- `@chirp/proto` → `file:../../../proto/ts`（ts-proto 产物 + protobufjs 运行时；
  proto/ts/package.json 是本批新增的包描述 shim，仅声明名与 exports，
  npm workspaces 未列该目录、对既有构建零影响）

## 验证边界（如实）

- 本机与 CI 均无 OHOS 工具链（hvigor/ohpm/hdc 缺位），本目录**未编译、未运行**。
- 协议核字节级对拍由 `apps/shared/protocol` 的既有 vitest 套件承载（帧/包/
  MsgID 表/管线/词库与本端消费的是同一份源码与同一份产物）；本端新增代码
  （平台适配层与 UI）无本地门禁。
- 编译腿（hvigor → HAP）与真机走查待 OHOS runner + 真机后补；届时若 ohpm 对
  file: 依赖的原始 .ts 源处理有出入，备案的回退方案是在本目录加一层 har 薄壳
  重导出（不复制源码）。
- 通知跳转、震动、互踢实景等真机面均未验证。
