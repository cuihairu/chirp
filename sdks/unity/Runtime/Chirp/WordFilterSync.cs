using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Chirp.Gateway;

namespace Chirp.Sdk
{
    /// <summary>词库下发同步——Unity C# 面的词库下发协议(MsgID 2245-2247),
    /// 与 TS/Android/iOS/C++ core 四端同一契约:包一个可热换的
    /// WordFilterInterceptor,FETCH_RESP / UPDATE_NOTIFY 载荷原地重建它,
    /// 发送侧预检跟随服务端词库走,而不是手工部署的词库文件。
    ///
    ///   * 版本门:Version 小于等于已应用版本的词库是重复投递或乱序到达,
    ///     忽略;
    ///   * Enabled=false(服务端不过滤)或策略 Record(只做服务端审计)时
    ///     清空本地词库、放行透传——客户端面永不比服务端更严;
    ///   * FetchAsync 失败静默返回 false(超时/断线/非 OK 都算),按提案
    ///     失败同步不得冒泡到 UI,兜底词库继续生效,下次连接再拉;
    ///   * LoadLocal 保留旧 WordFilterLoader 的本地兜底面,version 不动(0),
    ///     第一份服务端词库(版本 ≥ 1)到达即取代它。
    ///
    /// 线程模型:notify 分发与 RequestAsync 续体来自客户端接收泵(线程池),
    /// LoadLocal 通常在 Unity 主线程——inner/version 用 _gate 保护。注册
    /// 拦截器须在 ConnectAsync 之前,与 IMessageInterceptor 契约一致:
    ///   var sync = new WordFilterSync(client);
    ///   client.SetMessageInterceptor(sync);
    ///   // 登录成功后: sync.Start(); await sync.FetchAsync();</summary>
    public sealed class WordFilterSync : IMessageInterceptor
    {
        private readonly ChirpClient _client;
        private readonly object _gate = new object();
        private long _version;
        private WordFilterInterceptor? _inner;
        private Action? _unsubscribe;

        public WordFilterSync(ChirpClient client)
        {
            _client = client;
        }

        /// <summary>当前生效词库版本;0 = 尚未收到服务端词库。</summary>
        public long Version
        {
            get { lock (_gate) return _version; }
        }

        /// <summary>预检当前装载的词数(服务端词库或本地兜底)。</summary>
        public int WordCount
        {
            get { lock (_gate) return _inner?.WordCount ?? 0; }
        }

        /// <summary>订阅 UPDATE_NOTIFY。登录后调用一次——notify 与 fetch
        /// 都骑在已认证的会话上。重复 Start 幂等。</summary>
        public void Start()
        {
            lock (_gate)
            {
                if (_unsubscribe != null)
                {
                    return;
                }
            }
            // 订阅注册在锁外做;拿退订句柄后再抢锁落位——期间若 Stop 先行,
            // 句柄立即作废,不留悬空订阅。
            var unsub = _client.OnNotify(MsgID.WordFilterUpdateNotify, OnUpdateNotify);
            lock (_gate)
            {
                if (_unsubscribe != null)
                {
                    unsub();
                    return;
                }
                _unsubscribe = unsub;
            }
        }

        /// <summary>先摘回调再返回(在锁外执行退订),防与在途分发互等;
        /// 之后再 Start 会重新订阅。</summary>
        public void Stop()
        {
            Action? unsub;
            lock (_gate)
            {
                unsub = _unsubscribe;
                _unsubscribe = null;
            }
            unsub?.Invoke();
        }

        /// <summary>条件 GET:带本地已知版本拉全量词库(KnownVersion 命中
        /// 时回帧词库文本为空,走版本门自然忽略)。永不抛错——断线/超时/
        /// 非 OK 一律 false;阻塞由调用方选择 await,超时走
        /// Options.RequestTimeoutMs(可覆盖)。</summary>
        public async Task<bool> FetchAsync(int? timeoutMs = null)
        {
            try
            {
                var request = new Chirp.Chat.WordFilterFetchRequest
                {
                    KnownVersion = Version,
                };
                var response = await _client
                    .RequestAsync(Specs.WordFilterFetch, request, timeoutMs)
                    .ConfigureAwait(false);
                if (response.Code != Chirp.Common.ErrorCode.Ok)
                {
                    return false;
                }
                Apply(response.Lexicon);
                return true;
            }
            catch (Exception)
            {
                // RequestError(Closed/Timeout)、响应解析失败、服务端回包
                // 异常:失败同步不上抛,兜底词库继续生效。
                return false;
            }
        }

        /// <summary>本地兜底词库:按服务端文件格式给词行(旧 WordFilterLoader
        /// 面)。version 不动,首个服务端词库到达即取代。</summary>
        public void LoadLocal(IEnumerable<string> lines,
            WordFilterPolicy policy = WordFilterPolicy.Replace, string? replacement = null)
        {
            var inner = new WordFilterInterceptor(new WordFilterOptions
            {
                Terms = System.Linq.Enumerable.ToArray(lines),  // 快照:调用方后续改动不回灌
                Policy = policy,
                Replacement = replacement ?? "**",
            });
            lock (_gate)
            {
                _inner = inner;
            }
        }

        /// <summary>无词库生效(服务端未启用/仅审计)时透传。</summary>
        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request)
        {
            WordFilterInterceptor? inner;
            lock (_gate)
            {
                inner = _inner;
            }
            return inner == null || inner.OnBeforeSend(request);
        }

        private void OnUpdateNotify(byte[] body)
        {
            Chirp.Chat.WordFilterUpdateNotify notify;
            try
            {
                notify = Chirp.Chat.WordFilterUpdateNotify.Parser.ParseFrom(body);
            }
            catch (Exception)
            {
                return;  // 畸形 notify:忽略,下次 fetch 重同步。
            }
            Apply(notify.Lexicon);
        }

        /// <summary>版本门重建:小于等于已应用版本的词库是重复/乱序投递,
        /// 忽略。</summary>
        private void Apply(Chirp.Chat.WordFilterLexicon lexicon)
        {
            if (lexicon.Version <= Version)
            {
                return;
            }
            WordFilterInterceptor? inner;
            if (!lexicon.Enabled ||
                lexicon.Policy == Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyRecord)
            {
                inner = null;  // 服务端前置审计形态:客户端不设词库,永不比服务端更严
            }
            else
            {
                var policy = lexicon.Policy == Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReject
                    ? WordFilterPolicy.Reject
                    : WordFilterPolicy.Replace;
                inner = new WordFilterInterceptor(new WordFilterOptions
                {
                    Terms = SplitLines(lexicon.Lexicon),
                    Policy = policy,
                    // 服务端缺省 replacement 视为 "**"(四端同款)。
                    Replacement = string.IsNullOrEmpty(lexicon.Replacement) ? "**" : lexicon.Replacement,
                });
            }
            lock (_gate)
            {
                _version = lexicon.Version;
                _inner = inner;  // enabled=false / Record → 置空透传
            }
        }

        private static string[] SplitLines(string text)
        {
            // 词库文本按 '\n' 切(与 C++ core SplitLines 同款);空行/注释/
            // 大小写归一交给 ParseLexicon。
            var lines = new List<string>();
            var start = 0;
            while (true)
            {
                var pos = text.IndexOf('\n', start);
                if (pos < 0)
                {
                    if (start < text.Length)
                    {
                        lines.Add(text.Substring(start));
                    }
                    break;
                }
                lines.Add(text.Substring(start, pos - start));
                start = pos + 1;
            }
            return lines.ToArray();
        }
    }
}
