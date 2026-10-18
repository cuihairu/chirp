- [x] 审核整改（2026-10-18 独立审核，见 docs/review/luna-review-2026-10-18.md）：
      按审核报告的三张表逐项处理，**先核对再动手**，每项给 file:line 证据。
      1) 表一「缺陷」：按风险排序修，高风险优先
      2) 表二「文档与实现不一致」：以代码为准改文档，或明确标注为规划中
         **禁止为对齐文档去改业务代码**
      3) 表三「值得补的能力」：只做评估与分期建议，**不实现**（需用户拍板）
      每完成一项勾掉并写证据；全部测试通过后 commit+push；不 bump 版本号。
      **进度（2026-10-19）**：表一 P0/P1/P2 已清（TODO:267 `--host` 库层、269 六项、271 其余服务接线）；表二五项已清（TODO:270）；表三评估与分期建议已补齐（`docs/review/luna-review-2026-10-18.md` 表三节末新增 dated 评估节，含四行现状证据 + 分期 + 需拍板点 + 分期汇总）。**表一行 1 认证本体亦已清（TODO:268，机制已代决：共享密钥 `SERVER_AUTH_REQ` 握手，`--service_secret`/`--notification_secret` 成对）**。本整改伞全部收口；下一项「文档时效性清理」随后执行。
      **共享检出区脏文件收口（2026-10-19）**：工作区 24 个脏文件经核验为**早于已推送提交的废弃草稿**（改动时间 01:08–01:21，早于 `e336c9c` 01:49 与 `4fb8074` 02:50；TODO.md 侧该批把「余 ③⑤ 待做」标为未清，而 origin/main 已全清）——Redis 版设备持久化与「等待 MySQL 才回 OK」两案均已被文件版 DeviceStore 与「受理+有界重试+计数」语义覆盖取代，脏文档则通篇描述 Redis 方案（落了即违文档对账令）。已备份为 `discard-superseded-draft` 标签（385523b）后整体弃置，共享检出区快进至 `origin/main`；其中唯一未被取代的片段（开发 compose 不发布 5006/5016 到宿主机）并入 TODO:268 成果。

- [x] 文档时效性清理（2026-10-18 用户令：很多文档已过时）：
      本轮审核整改完成后执行。通读 README / docs，找出与代码不符的描述，以代码为准修正；
      **禁止为对齐文档去改业务代码**；已废弃功能明确标注或删除；命令行示例逐条验证；
      构建通过后 commit+push，回报改动文件清单。
      **已清（2026-10-19，见 git commit「docs: update stale service and sdk facts against code」差异集）**：四个 fork 审计簇（README/首页、guide、架构、API+SDK）逐主张对照 `2778f8a` 源码，23 项实证差异全修（明细入台账 R1022）：套件数 44→45、React 19+MUI 9、smoke 清单补 `--smoke-search`、`--search_host`/`--search_port` 归属修正、notification 调用方认证与 metrics 发布面同步 2778f8a、CHIRP_CRASH_* 环境变量族、CORE.md 虚构服务名改名、App Gateway 聚合面表述重写、npc_dialog 路径、web_companion 第五条 voice WS 与脚本路径、architecture 仓库分层补 social/voice/party/npc_dialog 与 7xxx 块、会话键 `<device>`→`<platform>`、overview 补 1020/2209/2210/2213/2214/错误码 14/party 小节、SDK 三套件当日实跑改数（sdk_core 140 例 10 套件 60 环回、Unity 141 例 + coverlet 同口径复测 98.5%(1739/1766)、TS vitest 149 例 10 文件）、Packet 四字段、Go `SetDevicePresenceHandler`、README 双语补「本地测试口径」对称节。历史时点记录（design-notes 等）按惯例不改写；未为对齐文档改任何业务代码。
