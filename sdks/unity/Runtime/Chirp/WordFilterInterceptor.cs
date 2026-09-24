using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Chirp.Sdk
{
    /// <summary>敏感词命中的两种客户端处置。没有 Record:记录待审核是
    /// 服务端职责(服务端 WordFilter 已实现三级策略),客户端预检的职责
    /// 只是不让脏词出门——要么改写,要么拦截。</summary>
    public enum WordFilterPolicy
    {
        /// <summary>命中区间改写为 Replacement(连续命中塌缩成一次替换)。</summary>
        Replace,
        /// <summary>命中即拒绝:OnBeforeSend 返回 false,SDK 报 Blocked。</summary>
        Reject,
    }

    public class WordFilterOptions
    {
        /// <summary>词集合,大小写不敏感;构造时统一 lower + 去重。</summary>
        public IReadOnlyCollection<string> Terms { get; set; } = Array.Empty<string>();
        public WordFilterPolicy Policy { get; set; } = WordFilterPolicy.Replace;
        public string Replacement { get; set; } = "**";
    }

    /// <summary>敏感词预检拦截器:行为对齐服务端 chirp::chat::WordFilter
    /// (词库格式、ASCII 大小写不敏感子串匹配、mask 后重建的替换语义),
    /// 套在 IMessageInterceptor 的发送钩子上,在消息出门前完成改写/拦截。
    /// 只滤发送侧:接收内容信任服务端已按其策略处理。Terms 构造后不可变,
    /// OnBeforeSend 可并发只读调用。</summary>
    public sealed class WordFilterInterceptor : IMessageInterceptor
    {
        private readonly string[] _terms;   // lower-cased, deduplicated, sorted
        private readonly WordFilterOptions _options;

        /// <param name="options">词库与策略;词集合随后不可变。</param>
        public WordFilterInterceptor(WordFilterOptions options)
        {
            _options = options;
            _terms = ParseLexicon(options.Terms);
        }

        public int WordCount => _terms.Length;

        /// <summary>按服务端词库文件格式(每行一词,# 开头为注释,空行
        /// 忽略)逐行解析。游戏把随包词库资源逐行喂进来,客户端与服务端
        /// 即可共用同一份词库。</summary>
        public static string[] ParseLexicon(IEnumerable<string> lines)
        {
            var unique = new SortedSet<string>();
            foreach (var raw in lines)
            {
                var term = raw.Trim(' ', '\r', '\n');
                if (term.Length == 0 || term[0] == '#')
                {
                    continue;
                }
                unique.Add(AsciiLower(term));
            }
            return unique.ToArray();
        }

        /// <summary>kReplace:命中时把改写后的内容写回 request 并放行;
        /// 无命中原样放行。kReject:命中返回 false(调用方得到
        /// RequestError(Blocked))。</summary>
        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request)
        {
            var rewritten = Filter(request.Content.ToStringUtf8(), out var hit);
            if (hit == FilterHit.Replaced)
            {
                request.Content = Google.Protobuf.ByteString.CopyFrom(
                    Encoding.UTF8.GetBytes(rewritten));
            }
            return hit != FilterHit.Blocked;
        }

        private enum FilterHit { Pass, Replaced, Blocked }

        private string Filter(string content, out FilterHit hit)
        {
            if (_terms.Length == 0)
            {
                hit = FilterHit.Pass;
                return content;
            }
            var lowered = AsciiLower(content);
            var hitStarts = new List<int>();
            foreach (var term in _terms)
            {
                var pos = lowered.IndexOf(term, StringComparison.Ordinal);
                while (pos >= 0)
                {
                    hitStarts.Add(pos);
                    pos = lowered.IndexOf(term, pos + term.Length, StringComparison.Ordinal);
                }
            }
            if (hitStarts.Count == 0)
            {
                hit = FilterHit.Pass;
                return content;
            }
            if (_options.Policy == WordFilterPolicy.Reject)
            {
                hit = FilterHit.Blocked;
                return content;
            }

            // Replace:逐命中区间打 mask 再重建——连续命中塌缩成一次
            // replacement,未命中字符保留原大小写(与服务端同款)。
            var masked = new bool[lowered.Length];
            foreach (var term in _terms)
            {
                var pos = lowered.IndexOf(term, StringComparison.Ordinal);
                while (pos >= 0)
                {
                    for (var i = pos; i < pos + term.Length; i++)
                    {
                        masked[i] = true;
                    }
                    pos = lowered.IndexOf(term, pos + term.Length, StringComparison.Ordinal);
                }
            }
            var filtered = new StringBuilder(content.Length);
            for (var i = 0; i < lowered.Length;)
            {
                if (masked[i])
                {
                    filtered.Append(_options.Replacement);
                    while (i < lowered.Length && masked[i])
                    {
                        i++;
                    }
                }
                else
                {
                    filtered.Append(content[i]);
                    i++;
                }
            }
            hit = FilterHit.Replaced;
            return filtered.ToString();
        }

        /// <summary>只折叠 ASCII 大小写,与服务端 ToLower(默认 C locale)
        /// 逐字节一致;UTF-8 多字节序列不受影响,按字符(UTF-16 code unit,
        /// BMP 内与字节序一致)匹配仍然成立。</summary>
        private static string AsciiLower(string text)
        {
            var builder = new StringBuilder(text.Length);
            foreach (var c in text)
            {
                builder.Append(c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c);
            }
            return builder.ToString();
        }
    }
}
