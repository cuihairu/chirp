import ChirpProtos
import Foundation

/// 词库下发同步：词库下发协议（docs/design-notes/word_filter.md「词库下发协议」，
/// MsgID 2245-2247）的 iOS/macOS 面。内部持一个可热换的 `WordFilter`：
/// FETCH_RESP / UPDATE_NOTIFY 载荷到达即重建，发送侧预检跟随服务端词库。
/// `WordFilterLoader.load` 的本地文本降级为回退：首个服务端词库
/// （version >= 1）到达即接管；服务端应答 version 0（未装配词库）时清空
/// 预检——客户端永远不比服务端更严。
///
/// 版本门：任何 version <= 本地版本的载荷视为乱序/重复投递，忽略。
/// record 是纯服务端审计语义，客户端不预检（透传）。
public final class WordFilterSync: MessageInterceptor {
    private let conn: ChatConnection
    private let timeoutMs: Int64

    private let mutex = NSLock()
    private var inner: WordFilter?
    private var version: Int64 = 0
    private var unsubscribe: (() -> Void)?

    public init(conn: ChatConnection, timeoutMs: Int64 = 10_000) {
        self.conn = conn
        self.timeoutMs = timeoutMs
    }

    deinit {
        stop()
    }

    /// 当前生效的服务端词库版本；0 = 尚未见过（本地回退不动它）。
    public var currentVersion: Int64 {
        mutex.lock(); defer { mutex.unlock() }
        return version
    }

    /// 预检当前词数（服务端词库或本地回退）。
    public var wordCount: Int {
        mutex.lock(); defer { mutex.unlock() }
        return inner?.wordCount ?? 0
    }

    /// 订阅 UPDATE_NOTIFY。登录成功后调一次。
    public func start() {
        unsubscribe = conn.onNotify(msgId: .wordFilterUpdateNotify) { [weak self] body in
            guard let self else { return }
            let notify: Chirp_Chat_WordFilterUpdateNotify
            do {
                notify = try Chirp_Chat_WordFilterUpdateNotify(serializedBytes: body)
            } catch {
                return // 坏载荷：忽略，下次 fetch 重新对齐
            }
            self.apply(notify.lexicon)
        }
    }

    public func stop() {
        mutex.lock()
        let off = unsubscribe
        unsubscribe = nil
        mutex.unlock()
        off?()
    }

    /// 本地文件回退：服务端格式的词库文本（原 `WordFilterLoader.load` 的输入）。
    /// 不动版本号，首个服务端词库照常接管。换行只按 \n 切——parser 本身会
    /// 再 trim 掉行尾 \r（与 Kotlin/服务端一致）。
    public func loadLocal(text: String, policy: WordFilterPolicy = .replace, replacement: String = "**") {
        let filter = WordFilter(options: WordFilterOptions(
            terms: WordFilter.parseWordLexicon(text.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)),
            policy: policy,
            replacement: replacement))
        mutex.lock()
        inner = filter
        mutex.unlock()
    }

    /// 条件 GET。resolve 后永不 throw（超时/断线/非 OK 一律 false）：提案规定
    /// 同步失败不上抛 UI——回退词库继续生效，下次连接再拉。
    public func fetch() -> Promise<Bool> {
        let known = currentVersion
        let request = Chirp_Chat_WordFilterFetchRequest.with { $0.knownVersion = known }
        return conn.request(
            spec: MsgSpecs.wordFilterFetch,
            body: request,
            timeoutMs: timeoutMs
        ).handle { [weak self] value, error in
            guard let self, error == nil, let resp = value, resp.code == Chirp_Common_ErrorCode.ok else {
                return false
            }
            self.apply(resp.lexicon)
            return true
        }
    }

    /// 无词库生效（服务端未启用或 record）时原样放行。
    public func onBeforeSend(
        _ request: Chirp_Chat_SendMessageRequest
    ) throws -> Chirp_Chat_SendMessageRequest? {
        mutex.lock()
        let current = inner
        mutex.unlock()
        guard let current else { return request }
        let original = String(decoding: request.content, as: UTF8.self)
        let result = current.filter(original)
        if !result.allowed { return nil } // nil = 拦截（管线转 blocked）
        if result.content == original { return request }
        var rewritten = request
        rewritten.content = Data(result.content.utf8)
        return rewritten
    }

    private func apply(_ lexicon: Chirp_Chat_WordFilterLexicon?) {
        guard let lexicon else { return }
        mutex.lock()
        guard lexicon.version > self.version else {
            mutex.unlock()
            return
        }
        self.version = lexicon.version
        // record 是纯服务端审计语义——客户端不预检；enabled=false 服务端不过滤，
        // 清掉本地（防「本地词库比服务端狠」的误拦）。
        if !lexicon.enabled || lexicon.policy == .wordFilterPolicyRecord {
            inner = nil
            mutex.unlock()
            return
        }
        let policy: WordFilterPolicy =
            lexicon.policy == .wordFilterPolicyReject ? .reject : .replace
        let replacement = lexicon.replacement.isEmpty ? "**" : lexicon.replacement
        inner = WordFilter(options: WordFilterOptions(
            terms: WordFilter.parseWordLexicon(lexicon.lexicon.split(separator: "\n").map(String.init)),
            policy: policy,
            replacement: replacement))
        mutex.unlock()
    }
}
