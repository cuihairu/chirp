# 批量决策分析 · 2026-10-30 · chirp

> 状态:已代决(2026-10-14 用户授权「按推荐拍,后续可推翻」);需要密钥/账号/用户私有数据的事项标「需用户输入,暂缓」,不硬做
>
> 范围:todo.md 中所有 BLOCKED / 待决策 / 缺用户输入 条目。分析基于仓库事实(file:line),不臆造。

---

## 1. iOS 推送(APNs)真投递

**现状**:仓内无待补实现——旗标面与出向 runbook 在位(`services/app/notification/src/main.cc:132-143`
`--apns-*` 旗标、`docs/guide/deployment.md` 出向段),无默认凭据。BLOCKED:缺 APNs 四项凭据
(Team ID / Key ID / `.p8` 私钥 / Bundle ID);生产另需出向 h2 反代(`--apns-endpoint` 指向它)。
取证:`find . -name '*.p8'` = 0,`$HOME` 下 3 个 `.p8` 均为 ring 测试密钥(非 APNs 凭据);`docker-compose*.yml` 无任何 `--apns-*`/凭据接线。

### 推荐决策
**暂缓** — 凭据类不代决;凭据到位后照 runbook 接线真机联调,验收 = 真实投递可达。

### 理由
- **成本**:实现面已齐,继续写只会重复;真正缺口是真实凭据与真机。
- **收益**:四项凭据到位后即可接线(测试用 sandbox 加 `--apns-sandbox`;生产用出向 h2 反代 + `--apns-endpoint`),真机联调验证真实投递。
- **风险(凭据)**:APNs 凭据涉真实推送渠道与生产密钥,属用户私有数据,不代决;无 `.p8` 实测不了 APNs 握手。
- **风险(假绿)**:mock 掉 APNs 只验证旗标装配,不验证推送链路;无 `.p8` 的"通过"是假验收。

### 立即执行动作
无。此项需用户输入:Team ID / Key ID / `.p8` 私钥 / Bundle ID(生产另需出向 h2 反代)。

### 明确不做 / 延后什么
- 不 mock 掉 APNs 当验收、不硬造凭据、不接线生产推送通道。

---

## 2. 消息压缩·鸿蒙 ArkTS 解压消费面

**现状**:消费面源码在位(`apps/harmony/entry/src/main/ets/common/HarmonyGzip.ts` 文件头自记 12 符号
已静态核对、`AppServices.ts` 装配,`build-profile.json5` 钉 `5.0.0(12)` / runtimeOS HarmonyOS);
iOS 半边已验证(`1a78f8e`,macOS runner run 38706660284 全绿,已声明 `supportsGzip`
= `ChatPipeline.swift:178`)。BLOCKED:缺 HarmonyOS NEXT 工具链(hvigor / ohpm / DevEco;
OpenHarmony SDK 不作数)。取证:`hvigorw/hvigor/ohpm/hdc/DevEco` PATH 全部 ABSENT;
`/opt/ohos-sdk` 仅 `native/` 切片(apiVersion 12,无 ets/API 层);`.github/workflows/` 无 ohos 引用。

### 推荐决策
**暂缓** — 等 HarmonyOS NEXT 工具链/runner;不降格用 OpenHarmony SDK,不声明未验证的平台支持。

### 理由
- **成本**:编译验证只差工具链;没有 hvigor/ohpm 无法构建 ArkTS 面。
- **收益**:装 HarmonyOS NEXT SDK + hvigor/ohpm(或提供 OHOS runner)即可编译 `HarmonyGzip.ts`,补齐鸿蒙半边(与 iOS 半边对称)。
- **风险(口径)**:OpenHarmony SDK 与 HarmonyOS NEXT 口径不同,`/opt/ohos-sdk` 仅有 native 切片、无 ets/API 层,不作数;用 OpenHarmony 编译出的"通过"不代表 HarmonyOS NEXT 行为。
- **不做**:不在未验证平台声明 `supports_gzip`(iOS 已声明,鸿蒙未验不声明)。

### 立即执行动作
无。此项需用户输入:安装 HarmonyOS NEXT SDK + hvigor/ohpm,或提供 OHOS runner。

### 明确不做 / 延后什么
- 不把 OpenHarmony SDK 当 HarmonyOS NEXT 验收、不声明鸿蒙 gzip 支持于未验状态。

---

*结论:本仓 BLOCKED 条目 2 项(APNs 真投递、鸿蒙 ArkTS 消费面),均为「需用户输入,暂缓」,
无免凭据立即动作。*
