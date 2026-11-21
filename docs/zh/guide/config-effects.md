> 🌐 Language: **中文** (this page) | [English](../../guide/config-effects.md)

# 配置开关效果

切换后会改变应用行为的开关，逐个给出 off→on 证据：开关控件本身的前后对比 + 两态交替播放的
GIF。本页全部产物由一个脚本再生——不要手工修改图片。

## 再生成

```bash
# 前置：apps/desktop/dev/stack-up.sh 栈在线 + vite dev server（5180）在线
node apps/desktop/dev/capture-switch-effects.mjs
```

产物落在 `docs/screenshots/switches/`。脚本断言每个开关真的翻转了
（contained ↔ outlined），没翻转会直接报错。

## 桌面端开关（已覆盖）

### 桌面通知（`通知`）

首次开启时向系统/浏览器请求通知权限；开启后，不在屏幕上的新消息发系统通知。

| 关 | 开 |
| --- | --- |
| ![桌面通知 关](../../screenshots/switches/desktop-notify-off.png) | ![桌面通知 开](../../screenshots/switches/desktop-notify-on.png) |

![桌面通知 动图](../../screenshots/switches/desktop-notify.gif)

采集说明：无头 Chromium 会把 Notification 权限硬判 denied，采集桩与单测同款打了桩。
GIF 取证的是控件 UI 状态翻转；真实桌面壳里的权限申请走 Tauri 通知插件。

### 好友上线通知（`上线通知`）

好友上线时弹 toast（social 平面）。

| 关 | 开 |
| --- | --- |
| ![上线通知 关](../../screenshots/switches/friend-online-notify-off.png) | ![上线通知 开](../../screenshots/switches/friend-online-notify-on.png) |

![上线通知 动图](../../screenshots/switches/friend-online-notify.gif)

### 私聊限制（`私聊限制`）

开启后仅好友/同群成员可私信我（服务端强制）。

| 关 | 开 |
| --- | --- |
| ![私聊限制 关](../../screenshots/switches/dm-restriction-off.png) | ![私聊限制 开](../../screenshots/switches/dm-restriction-on.png) |

![私聊限制 动图](../../screenshots/switches/dm-restriction.gif)

## 尚未覆盖

- `web_companion` 的开关——等它的 dev rig 也脚本化后，用同一条采集管线扩展。
- 服务端 CLI 旗标（`--push_transport`、`--apns-*`、限流参数等）——off→on 证据需要脚本化的
  双跑服务端对照（日志/行为取证）；本机尚未安装 `asciinema`/`test-electron`，终端式录制暂缺。
