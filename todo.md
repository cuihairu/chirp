- [x] 审核整改（2026-10-18 独立审核，见 docs/review/luna-review-2026-10-18.md）：
      按审核报告的三张表逐项处理，**先核对再动手**，每项给 file:line 证据。
      1) 表一「缺陷」：按风险排序修，高风险优先
      2) 表二「文档与实现不一致」：以代码为准改文档，或明确标注为规划中
         **禁止为对齐文档去改业务代码**
      3) 表三「值得补的能力」：只做评估与分期建议，**不实现**（需用户拍板）
      每完成一项勾掉并写证据；全部测试通过后 commit+push；不 bump 版本号。
      **进度（2026-10-19）**：表一 P0/P1/P2 已清（TODO:267 `--host` 库层、269 六项、271 其余服务接线）；表二五项已清（TODO:270）；表三评估与分期建议已补齐（`docs/review/luna-review-2026-10-18.md` 表三节末新增 dated 评估节，含四行现状证据 + 分期 + 需拍板点 + 分期汇总）。**表一行 1 认证本体亦已清（TODO:268，机制已代决：共享密钥 `SERVER_AUTH_REQ` 握手，`--service_secret`/`--notification_secret` 成对）**。本整改伞全部收口；下一项「文档时效性清理」随后执行。
      **共享检出区脏文件收口（2026-10-19）**：工作区 24 个脏文件经核验为**早于已推送提交的废弃草稿**（改动时间 01:08–01:21，早于 `e336c9c` 01:49 与 `4fb8074` 02:50；todo.md 侧该批把「余 ③⑤ 待做」标为未清，而 origin/main 已全清）——Redis 版设备持久化与「等待 MySQL 才回 OK」两案均已被文件版 DeviceStore 与「受理+有界重试+计数」语义覆盖取代，脏文档则通篇描述 Redis 方案（落了即违文档对账令）。已备份为 `discard-superseded-draft` 标签（385523b）后整体弃置，共享检出区快进至 `origin/main`；其中唯一未被取代的片段（开发 compose 不发布 5006/5016 到宿主机）并入 TODO:268 成果。

- [x] 文档时效性清理（2026-10-18 用户令：很多文档已过时）：
      本轮审核整改完成后执行。通读 README / docs，找出与代码不符的描述，以代码为准修正；
      **禁止为对齐文档去改业务代码**；已废弃功能明确标注或删除；命令行示例逐条验证；
      构建通过后 commit+push，回报改动文件清单。
      **已清（2026-10-19，见 git commit「docs: update stale service and sdk facts against code」差异集）**：四个 fork 审计簇（README/首页、guide、架构、API+SDK）逐主张对照 `2778f8a` 源码，23 项实证差异全修（明细入台账 R1022）：套件数 44→45、React 19+MUI 9、smoke 清单补 `--smoke-search`、`--search_host`/`--search_port` 归属修正、notification 调用方认证与 metrics 发布面同步 2778f8a、CHIRP_CRASH_* 环境变量族、CORE.md 虚构服务名改名、App Gateway 聚合面表述重写、npc_dialog 路径、web_companion 第五条 voice WS 与脚本路径、architecture 仓库分层补 social/voice/party/npc_dialog 与 7xxx 块、会话键 `<device>`→`<platform>`、overview 补 1020/2209/2210/2213/2214/错误码 14/party 小节、SDK 三套件当日实跑改数（sdk_core 140 例 10 套件 60 环回、Unity 141 例 + coverlet 同口径复测 98.5%(1739/1766)、TS vitest 149 例 10 文件）、Packet 四字段、Go `SetDevicePresenceHandler`、README 双语补「本地测试口径」对称节。历史时点记录（design-notes 等）按惯例不改写；未为对齐文档改任何业务代码。

- [x] README 徽章在 HTML 块内不渲染，改为 img 标签（2026-10-27）：
      `4d8c5f9`。README.md / README.zh.md 的 `<p align="center">` 徽章块内 4 个 `![alt](url)` 全部改 `<img alt src>`（GitHub 不解析 HTML 块内 markdown，原写法线上显示原文）；块内 `grep -c '!\['` 两文件均 0，徽章仍单行，logo/标题/版本号未动。


## 合并自 todo.md（2026-10-28，原文件未勾项摘录，随后删除）

## 移动端原生技术栈迁移（2026-09-28 启动，Android M1+M2 完成）

- [ ] iOS 推送（APNs）真投递——等用户凭据（Team ID / Key ID / .p8 私钥 / Bundle ID，凭据类不代决）；凭据到位后照 `docs/guide/deployment.md` 推送出向段接线（`--push_transport http` + `--apns-*` 旗标）真机联调，验收＝真实投递可达（生产需出向 h2 反代，`--apns-endpoint` 指向它；仓内无待补实现）。BLOCKED 缺 APNs Team ID / Key ID / .p8 私钥 / Bundle ID（及生产出向 h2 反代）。
      已代决:按 docs/review/2026-10-30-decisions.md 执行(2026-10-30)——暂缓待用户四项凭据(凭据类不代决);旗标面/出向 runbook 在位,凭据到位照 runbook 接线真机联调,不 mock APNs 验收
      证据（2026-10-28 巡检，BLOCKED 待用户凭据）：仓内 `find . -name '*.p8'`＝0，`$HOME` 下亦 0；`docker-compose.yml` 与 `deployments/docker/docker-compose.deploy.yml` 无任何 `--apns-*`/凭据接线（grep matches 0）；`services/app/notification/src/main.cc:132-143` 旗标面与 `docs/guide/deployment.md` 出向 runbook 在位、无默认凭据。手动动作：用户给四项凭据（sandbox 加 `--apns-sandbox`；生产加出向 h2 反代 + `--apns-endpoint` 指向它），再按 runbook 接线真机联调。

## 近期批次（2026-10-08 起）

- [ ] 消息压缩·鸿蒙 ArkTS 解压消费面——待 OHOS 工具链（hvigor/ohpm/DevEco，HarmonyOS NEXT 口径——OpenHarmony SDK 不作数）编译验证；iOS 半边已验证（`1a78f8e`，macOS runner run 38706660284 全绿，登录已声明 `supportsGzip`＝ChatPipeline.swift:178）。BLOCKED 缺 HarmonyOS NEXT 工具链/runner（hvigor、ohpm、DevEco；OpenHarmony SDK 不作数）。
      已代决:按 docs/review/2026-10-30-decisions.md 执行(2026-10-30)——暂缓待 HarmonyOS NEXT 工具链/runner;消费面源码在位,工具链到位首务编译 HarmonyGzip.ts;不降格用 OpenHarmony SDK、不声明未验平台支持
      证据（2026-10-28 巡检，BLOCKED 待 OHOS 工具链）：`hvigorw/hvigor/ohpm/hdc/DevEco` PATH 探测全部 ABSENT；`/opt/ohos-sdk` 仅含 `native/` 切片（apiVersion 12 / 5.0.0.71，无 ets/API 层），无论口径均不覆盖 ArkTS 构建面；`.github/workflows/` 无 ohos/hvigor/ohpm/harmony 引用（matches 0）。消费面源码在位：`apps/harmony/entry/src/main/ets/common/HarmonyGzip.ts`（文件头自记 12 符号已静态核对、编译验证待工具链）+ `AppServices.ts` 装配，`build-profile.json5` 钉 `5.0.0(12)` / runtimeOS HarmonyOS。手动动作：装 HarmonyOS NEXT SDK + hvigor/ohpm（或提供 OHOS runner），到位首务即编译 `HarmonyGzip.ts`。

## 审核发现

- [x] 审核发现：提交宣称修复 todo.md 旧引用，但仓内仍残留大写 `TODO.md` 引用，指向已不存在文件（libs/network/service_bridge.h:49）
      **已清（本 commit）+证据**：`libs/network/service_bridge.h:49` 大写 `TODO.md`→小写 `todo.md`（纯注释，无逻辑变更）；复查 `rg -n 'TODO\.md'` 全仓仅剩本行自指，且 `TODO.md` 不存在、`todo.md` 在位。属实已修。
- [x] 审核发现：Defender 排除路径 `D:\.pnpm-store` 不匹配 GitHub Windows runner 的默认 pnpm store（位于 `%LOCALAPPDATA%\pnpm\store`，即 `C:\Users\runneradmin\...`），仓内也无 store-dir 配置重定向，该排除实际盖不到 store，唯一生效的防护只剩 `packageImportMethod: copy`（.github/workflows/nightly.yml:213）
      **已清（本 commit）+证据**：`.github/workflows/nightly.yml:217-218` 排除路径 `'D:\.pnpm-store'`→`"$env:LOCALAPPDATA\pnpm\store"`（Windows 默认 store 即 `%LOCALAPPDATA%\pnpm\store`）；`D:\a` 检出区排除保留。属实已修。
- [x] 审核发现：仅 Windows nightly 的 Defender 锁问题，却把 `packageImportMethod: copy` 写进根与 apps/desktop 两处 workspace 配置，令全平台及所有本地/CI 安装都改为 copy 导入（弃硬链接、增盘增时），波及面超出被修的 Windows 腿；Windows 专病不应全局下药（pnpm-workspace.yaml:31，apps/desktop/pnpm-workspace.yaml:12）
      **已清（本 commit）+证据**：`pnpm-workspace.yaml`（原 :31）与 `apps/desktop/pnpm-workspace.yaml`（原 :12）两处 `packageImportMethod: copy` 均已删，全局下药撤除；改由 `.github/workflows/nightly.yml:215` 仅 Windows 腿经 `GITHUB_ENV` 写 `pnpm_config_package_import_method=copy`（非 Windows 腿不跑该步，保持默认硬链接）。实测 pnpm 12.10.1：`pnpm_config_*` 环境变量生效（`config get` 返回 `copy`），而 `npm_config_*` 被忽略（返回 `undefined`）；真实安装链接数验证：默认＝硬链接（link count 4），带该环境变量＝copy（link count 1）。属实已修。
- [x] 审核发现：CI 无任何断言校验 `packageImportMethod: copy` 确被 pnpm 采纳（如核对 node_modules 链接类型或 `pnpm config get`），若 pnpm 不再读该键或键名写错，防护静默失效，os error 32 只在深夜才再爆发（.github/workflows/nightly.yml:210）
      **已清（本 commit）+证据**：`.github/workflows/nightly.yml:237-246` 新增「Assert pnpm copy import is active」步（`if: runner.os=='Windows'`，`shell: bash`），断言根与 `apps/desktop` 两处 `pnpm config get packageImportMethod` 均 `='copy'`；pnpm 不再读必致失败。属实已修。
- [x] 审核通过：1e62b39c..de5a53a 无发现
- [x] 审核发现：de5a53a..HEAD 未发现问题
       **已核（2026-10-29，复核 2026-10-29 修订）+证据**：审核时点固定区间 `git log --format='%H %s' de5a53a..85c4ee4`＝`85c4ee4 docs: fix audit findings on de5a53a..HEAD range evidence`＋`0aafd03 docs: verify no findings in de5a53a..HEAD range`＋`9381586 docs: mark blocked ledger items for apns and arkts` 三行；`git diff --numstat de5a53a..85c4ee4`＝`9	2	todo.md`（9+/2-，stat 头 11 行，纯账本标注，无代码/构建/测试改动），区间内无问题发现。属实已核。
- [x] 审核发现：`todo.md:45` 证据称 `de5a53a..HEAD` 仅含 `9381586` 一个提交，实际还含 `0aafd03`（`git log de5a53a..HEAD` 为 `0aafd03`+`9381586` 两行），文档与实现不一致（todo.md:45）
      **已清（本 commit）+证据**：`todo.md:45` 已改为列全两行提交（`0aafd03`＋`9381586`）；复跑 `git log --format='%H %s' de5a53a..85c4ee4`＝`85c4ee4 docs: fix audit findings on de5a53a..HEAD range evidence`＋`0aafd039... docs: verify no findings in de5a53a..HEAD range`＋`9381586c... docs: mark blocked ledger items for apns and arkts` 三行，与所记一致。属实已修。
- [x] 审核发现：`todo.md:45` 证据称 `git diff --stat de5a53a..HEAD` 为「5 行，3+/2-」，实际为「5 行新增，2 行删除」（5+/2-），插入行数统计错误（todo.md:45）
      **已清（85c4ee4 后续修订）+证据**：`todo.md:45` 已改为 `git diff --numstat de5a53a..85c4ee4` 实测 `9	2	todo.md`（9+/2-，stat 头为 `todo.md | 11 +++++++++--`）；复跑输出与所记一致。属实已修。
- [x] 审核发现：新增证据把 `de5a53a..HEAD` 的提交列表写成 `0aafd03`＋`9381586` 两项，遗漏了当前 `HEAD` `85c4ee4`，实际为三项（todo.md:45）
      **已清（本 commit）+证据**：`todo.md:45` 已改为固定区间 `de5a53a..85c4ee4` 并列全三项提交（`85c4ee4`＋`0aafd03`＋`9381586`）；复跑 `git log --format='%H %s' de5a53a..85c4ee4` 实得同三行，与所记一致。属实已修。
- [x] 审核发现：新增证据把 `de5a53a..HEAD` 的差异写成 `todo.md` 7 行、5+/2-，实际为 11 行、9+/2-（`9	2	todo.md`）（todo.md:45）
      **已清（本 commit）+证据**：`todo.md:45` 已改为固定区间 `git diff --numstat de5a53a..85c4ee4` 实测 `9	2	todo.md`（9+/2-，stat 头为 `todo.md | 11 +++++++++--`）；复跑输出与所记一致。属实已修。

- [x] 审核发现：决策文档取证断言「`$HOME` 下亦 0」不实，实测 `find "$HOME" -name '*.p8'` 返回 3 个文件（`~/.cargo/registry/.../ring-0.17.14/tests/` 下 ring 测试密钥，非 APNs 凭据但属 `.p8`），表述与仓库事实不符（docs/review/2026-10-30-decisions.md:14）
      **已清（本 commit）+证据**：复跑 `find "$HOME" -name '*.p8'` 实得 3 文件（`~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/ring-0.17.14/tests/` 下 `ecdsa_test_private_key_p256.p8`、`rsa_test_private_key_2048.p8`、`ed25519_test_private_key.p8`，均为 ring 测试密钥，非 APNs 凭据）；`docs/review/2026-10-30-decisions.md:14` 已改为「`$HOME` 下 3 个 `.p8` 均为 ring 测试密钥(非 APNs 凭据)」。属实已修。
- [x] 审核发现：决策文档称 iOS 已声明 `supports_gzip`＝`ChatPipeline.swift:178`，但该行 Swift 属性名实为 `supportsGzip`（`request.supportsGzip = true`；`supports_gzip` 仅 proto 字段名），符号名引用与源码不符（基线 1b47be8:docs/review/2026-10-30-decisions.md:37）
      **已清（本 commit）+证据**：`apps/ios/Sources/ChirpProtocol/ChatPipeline.swift:178` 实测 `request.supportsGzip = true`（`supports_gzip` 仅 proto 字段名，见 `auth.pb.swift:53` `public var supportsGzip`）；`docs/review/2026-10-30-decisions.md:39-40` 已改为「已声明 `supportsGzip`＝`ChatPipeline.swift:178`」。属实已修。
- [x] 审核发现：todo.md:59-60 新证据引用 `docs/review/2026-10-30-decisions.md:37-38`/`:38` 指向 supportsGzip 语句，但 HEAD 中该语句已移至 `:39-40`（`17f27b4` 插入「待用户」块引用致 +2 行偏移），且基线 `supports_gzip` 符号实际在 `:37` 而非 `:38`，行号引用与文件事实不符（todo.md:59-60）
      **已清（本 commit）+证据**：复跑 `git show HEAD:docs/review/2026-10-30-decisions.md | nl -ba | sed -n '39,40p'`＝`39  iOS 半边已验证(...已声明 supportsGzip`／`40  = ChatPipeline.swift:178...`（`17f27b4` 插「待用户」块致 +2 偏移）；基线 `git show 1b47be8:docs/review/2026-10-30-decisions.md | nl -ba | sed -n '37p'`＝`37  iOS 半边已验证(...已声明 supports_gzip`，即基线符号仅在 `:37`。todo.md:59 引用改基线 `1b47be8:37`（supports_gzip 符号仅基线存在，HEAD `:37` 为「消费面源码在位」语句）、todo.md:60 引用改 `:39-40`，均与文件事实一致。属实已修。
- [x] 审核通过：17f27b47a568a76903ab728583ae20737b2bb799..HEAD 未发现问题
- [x] 审核发现：e760e17 改 todo.md:59 引用为 docs/review/2026-10-30-decisions.md:37 未限定提交版本，HEAD 该行实为「消费面源码在位」语句而非所描述的 supports_gzip 声明（该符号仅基线 1b47be8:37 存在），且 todo.md:62「均与文件事实一致」对 :37 不成立（todo.md:59）
      **已清（本 commit）+证据**：todo.md:59 引用已改「基线 1b47be8:docs/review/2026-10-30-decisions.md:37」（限定提交版本）；复跑 `git show HEAD:docs/review/2026-10-30-decisions.md | nl -ba | sed -n '37p'`＝`37  **现状**:消费面源码在位(...`（HEAD `:37` 确为「消费面源码在位」语句）；`git show 1b47be8:docs/review/2026-10-30-decisions.md | nl -ba | sed -n '37p'`＝`37  iOS 半边已验证(...已声明 supports_gzip`（supports_gzip 符号仅基线 `1b47be8:37` 存在）。属实已修。
      （原「审核发现：本轮 diff 未见问题（17f27b4..HEAD）」行已删，不属实：区间 `17f27b4..HEAD` 实含 `e760e17`，而 `e760e17` 引入 todo.md:59 行号引用缺陷（上行已修），故「本轮 diff 未见问题」断言不成立。）
