# 敏感词过滤（word filter）算法规约

聊天消息的敏感词过滤在**服务端强制执行**，客户端做发送侧预检。本文是四端共享的
算法规约——所有实现必须逐字节对拍同一语义，客户端裁决才能预测服务端裁决。

## 实现矩阵

| 端 | 文件 | 角色 |
|---|---|---|
| C++ 服务端（真源） | `services/shared/chat/src/word_filter.{h,cc}` | 聊天服务强制执行点，三策略 + 词库热更新 |
| C++ core SDK | `sdks/core/include/chirp/{word_filter.h,word_filter_sync.h}` | 发送侧预检（interceptor）+ 词库下发同步 |
| C# (Unity) | `sdks/unity/Runtime/Chirp/{WordFilterInterceptor.cs,WordFilterSync.cs}` | 发送侧预检（interceptor）+ 词库下发同步 |
| Kotlin (Android 原生) | `apps/android/src/main/kotlin/chirp/mobile/protocol/WordFilter.kt` | 发送侧预检（双门禁共用：JVM make 腿 + Gradle 腿） |
| Swift (iOS) | `apps/ios/Sources/ChirpProtocol/WordFilter.swift` | 发送侧预检（匹配按 UTF-16 code unit 与 Kotlin 对齐） |

测试对拍约定：各端测试使用**同一组向量**（Kotlin
`apps/android/src/test/kotlin/chirp/mobile/protocol/WordFilterTest.kt`、Swift
`apps/ios/Tests/ChirpProtocolTests/WordFilterTests.swift` 等；Dart/Flutter SDK
已随 2026-09-29 移除，其 `word_filter_test.dart` 不复存在）。改语义先改本文，再各端同步实现与向量。

## 算法

一句话：**ASCII 折叠小写后做多模式朴素子串扫描，命中区间写入掩码，按掩码重建
原文（连续命中塌缩成一次替换）**。不是 Aho-Corasick，不是正则。

### 1. 词库解析（LoadLexicon / parseWordLexicon）

- 文件格式：每行一词；`#` 开头的行是注释；空行忽略
- trim 集合与 C locale 一致：仅行尾的 空格/CR/LF 与行首空格（**tab 不 trim**）
- 每词做 ASCII 折叠小写后**去重 + 排序**（排序只为遍历确定性，不影响语义）
- 空词库 ⇒ 过滤器整体禁用（`enabled == false`，Filter 恒放行）

### 2. 大小写折叠（ToLower / asciiLower）

**只折叠 ASCII** `0x41–0x5A → 0x61–0x7A`，其余字节原样。这是对齐服务端
`std::tolower` 在 C locale 下的字节语义：UTF-8 多字节序列（中文等）完全不被
触碰，匹配按字节/code unit 进行。客户端实现把这个折叠**硬编码**为 ASCII-only
（dart/Kotlin 无 locale 概念，也避免宿主 locale 污染），语义等价且更稳。

### 3. 匹配（Filter 的扫描段）

在折叠副本上，对每个词做 `find` 循环扫描；每次命中从 `命中位置 + 词长` 处继续
（同一词的重叠出现不重复计）。所有词的所有命中位置构成命中集合；命中集合为空
⇒ 放行原文。

复杂度 O(词数 × 文本长 × 词长)。设计词库量级是游戏词表的「百」级、消息百字符
级，总工作量微秒级——朴素扫描的常数与可解释性优先于渐近复杂度。

### 4. 掩码重建（kReplace 路径）

- 所有命中区间写入长度 = 原文长度的布尔掩码（区间是**并集**）
- 按掩码重建：未命中字节保留**原文大小写**；连续命中 run 塌缩成**一次**替换符
  （词库 `{ab, bc}` 对 `"abc"` → `"#"`，不是 `"##"`）

### 5. 策略（服务端三级，客户端两级）

| 策略 | 行为 | 客户端是否实现 |
|---|---|---|
| `replace`（默认） | 命中区间掩码改写后放行 | ✅ |
| `reject` | 任一命中即拒绝（服务端回 `INVALID_PARAM`；游戏面专码 `WORD_FILTERED=10`） | ✅ |
| `record` | 原样放行，仅记日志供事后审计 | ❌（纯服务端运维面，客户端无意义） |

策略解析大小写不敏感；未知串**回退 replace**。替换符可配，默认 `"**"`。

## 服务端运行时行为

- **词库热更新**：mtime 变化即整表重载，检查节流 `reload_check_interval_ms`
  （默认 5s）——改文件无需重启
- **fail-open**：词库缺失/不可读 → 空表 + Warn 日志，聊天继续（过滤器挂了不
  连带聊天挂）
- **线程模型**：只允许 service io 线程调用（发送路径单线程），惰性重载状态
  不加锁
- 词库行缓冲 512 字节（`fgets`）：**超过 511 字节的词条会被截断成多行**——
  词表里别放超长条目

## 设计取舍（FAQ）

**为什么不让用户/调用方选匹配算法？**
过滤的「用户」是运营者不是聊天用户，算法是契约不是配置。可配置面是策略、替换
符、词库内容；算法一旦多选，客户端预检与服务端裁决就可能分叉（客户端放行服务
端拒 = 浪费往返 + 报错困惑），且四端 × N 算法的等价性证明成本爆炸。

**为什么不用 Aho-Corasick？**
规模不需要（见复杂度段）。且将来词库涨到性能敏感量级时，AC 与朴素扫描对同一
词库产生**完全相同的命中区间并集**——把 `maskHits` 内部换成 AC 自动机，
`Filter` 返回值逐字节不变。即算法可以作为内部实现细节升级（保语义契约），不
作为外部选项存在。

**谐音/拼音/变体字怎么办？**
不靠匹配算法解决：服务端加归一化层（拼音表/变体映射先折叠原文再进同一匹配
器），匹配语义仍然只有一份。

**为什么客户端知道结果还要发？**
客户端预检只是**预测**，服务端是唯一裁决点（词库/策略随时可改且不下发给客
户端）。预测失败的最坏情形是多一次往返拿 `INVALID_PARAM`。

## 词库下发协议（2026-10 提案，已落地）

### 背景

服务端词库长期只有 `--word_filter_file` 本地装载（mtime 惰性热更新），客户端
预检词库靠部署侧人工同步（Android 壳读 `filesDir/word_filter.txt`，iOS/TS 由
调用方构造）——两端词库一旦漂移，客户端预检就预测不准服务端裁决（多浪费往返、
reject 策略下还造成客户端能发服务端拒的困惑）。本提案给词库立一条下发通道：
客户端预检词库以下发源为准，本地文件降级为回退。

### 消息（proto/gateway.proto MsgID 2245-2247，body 见 proto/chat.proto）

| MsgID | 名称 | 方向 | 语义 |
|---|---|---|---|
| 2245 | `WORD_FILTER_FETCH_REQ` | 客户端 → 服务端 | 拉取，body `WordFilterFetchRequest{known_version}` |
| 2246 | `WORD_FILTER_FETCH_RESP` | 服务端 → 客户端 | 应答拉取，body `WordFilterFetchResponse{code, lexicon}` |
| 2247 | `WORD_FILTER_UPDATE_NOTIFY` | 服务端 → 客户端 | 热更新推送，body `WordFilterUpdateNotify{lexicon}`，sequence 0、只读、不要求 ACK |

`WordFilterLexicon`（RESP 与 NOTIFY 共用载荷）：`version` + `enabled` +
`policy` + `replacement` + `lexicon`（规范化词库文本）。

### 语义规约

1. **版本单调**：服务端每次装载词库（构造首载、mtime 热更新重载、重载失败
   变空表）`version` 自增，从 1 起；`0` 表示服务端没有装配词库。客户端忽略
   `version <= 本地版本` 的任何下发——乱序与重复投递免疫。
2. **拉取 = 条件 GET**：客户端带 `known_version`（0 = 没有）；服务端版本一致
   时 RESP 省略 `lexicon` 文本（省流量），客户端按 version 相等跳过装载。
3. **推送 = 热更新广播**：服务端词库 mtime 变化重载成功后，向全部已认证在线
   会话广播完整新词库（锁内快照、锁外投递，对齐设备清单广播）。没拉取过的
   端收到推送也照常装载——推送自足，不依赖先拉取。
4. **文本格式不变**：`lexicon` 文本是服务端词库的规范化形式（每行一词、小写、
   去重排序），与 `--word_filter_file` 同格式，客户端照常走 `parseWordLexicon`。
5. **策略随词库下发**：客户端预检镜像服务端策略——`replace` 改写、`reject`
   拒发；`record` 是纯服务端审计语义，客户端**不预检**（透传，避免客户端拦了
   服务端却放行的体验分叉）。
6. **鉴权与守卫**：FETCH 未登录回 `AUTH_FAILED`，垃圾 body 回 `INVALID_PARAM`
   （对齐 2239-2244 段守卫惯例）；词库不是机密但不开给未认证连接。
7. **服务端未启用词库**（无 `--word_filter_file`）：FETCH 照常应答
   `version=0, enabled=false, 空文本`；客户端收到后清空本地预检词库（服务端
   都不过滤，预检没有意义，且本地词库可能比服务端新——防止客户端误拦）。

### 客户端接收（五端 SDK，`WordFilterSync` 组件）

下发源成为首选，`WordFilterLoader` 本地文件降级为回退：

| 端 | 组件 | 拉取时机 | 回退 |
|---|---|---|---|
| TS | `sdks/ts/src/word_filter_sync.ts` | 应用在登录成功后调 `fetch()` | `loadLocal(lines)` |
| C++ core | `sdks/core/include/chirp/word_filter_sync.h` | 调用方在登录成功后 `Fetch()`（`Start()` 订阅推送） | `LoadLocal(lines)` |
| Unity C# | `sdks/unity/Runtime/Chirp/WordFilterSync.cs` | 调用方在登录成功后 `FetchAsync()`（`Start()` 订阅推送） | `LoadLocal(lines)` |
| Kotlin | `apps/android/.../protocol/WordFilterSync.kt` | 壳层登录成功后 `fetch()` | `loadLocal(lines)`（原 `WordFilterLoader` 读的文件行） |
| Swift | `apps/ios/Sources/ChirpProtocol/WordFilterSync.swift` | 调用方在登录成功后 `fetch()` | `loadLocal(lines)`（原 `WordFilterLoader` 读的文本行） |

组件自身实现各端 `MessageInterceptor`：内部持当前 `WordFilter`（可热换），
`FETCH_RESP` / `UPDATE_NOTIFY` 到达即重建——预检立即跟随服务端词库。`fetch()`
失败（断线/超时/非 OK）不抛给 UI，回退词库继续生效，下次连接再拉。

### 服务端接线

- **basic**（`main.cc`）：`HandlePacket` switch 新增 `WORD_FILTER_FETCH_REQ`
  case，词库状态填充与条件 GET 判定在共享单元
  `services/shared/chat/src/word_filter_push.{h,cc}`；
- **enhanced**（`main_enhanced.cc`）：`on_packet` 分发在
  `DispatchDistributedPacket` 前拦截同款处理（SERVER_AUTH_REQ 先例）；
- **广播**：两形态 `main()` 各挂 `WordFilter::set_on_reload` 回调 →
  `BroadcastWordFilterUpdate`（遍历 registry 全部活会话）；
- distributed 形态本就未接词库（无过滤），不接下发。

### 设计取舍

- **版本号 + 全量文本，不做哈希两步拉取**：游戏词库量级是几百项、几 KB，一轮
  拉取就完事；哈希协商把一次往返变两次，还要客户端持久化哈希状态，收益为负。
- **推送不做增量**：同上，全量文本是自足载荷，客户端不维护补丁状态。
- **record 策略客户端不预检**：见语义规约 5——预检的职责是预测裁决，record
  没有可预测的拒绝行为。
- **服务端未启用时清空客户端词库**：防「本地词库比服务端狠」的误拦——服务端
  放行的消息客户端拦住，比漏拦更伤体验。
