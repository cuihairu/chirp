using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Google.Protobuf;

namespace Chirp.Sdk
{
    /// <summary>文件版本地存档:append-only 日志 + 启动重放,零外部依赖
    /// (序列化直接用管线里已有的 Google.Protobuf 运行时),Unity 工程
    /// 拖入 Runtime/Chirp 即可编译。语义对齐 MemoryMessageStore(分桶
    /// newest-first、超限淘汰最旧),并额外提供持久化与已读跟踪:
    /// - Save 追加一条 [kind|len|payload] 记录;进程崩溃最多丢尾部半条
    ///   (重放时忽略),已落盘记录不受影响。
    /// - MarkRead 追加一条已读记录;GetUnreadCount = 该频道内"服务端
    ///   已回填 messageId、且未标已读"的条数(发送侧本地存档的
    ///   messageId 为空,天然不计未读——与 IMessageStore 契约一致)。
    /// - 日志只增不删:超限淘汰与 Cleanup 先作用于内存索引;Cleanup
    ///   与显式 Compact() 会把当前内存快照原子重写回文件(tmp +
    ///   File.Replace),此后被淘汰/清理的条目不再复活。只靠超限淘汰
    ///   而从未 Compact 的频道,重启后可能看到被淘汰的旧条目——需要
    ///   紧收历史时调 Compact(重写成本 O(存量),勿在高频路径调用)。
    /// - 文件头不是 CHIRPLOG1(空文件/他源文件)时按损坏处理:重置为
    ///   空档案,不抛异常。
    /// 全部方法加锁,任意线程可调(与 MemoryMessageStore 相同;IO 在
    /// 锁内完成,聊天消息频率下可接受)。</summary>
    public sealed class FileMessageStore : IMessageStore
    {
        private const string Magic = "CHIRPLOG1";
        private const byte KindMessage = 0x01;
        private const byte KindRead = 0x02;

        private readonly object _gate = new object();
        private readonly string _path;
        private readonly int _maxPerChannel;
        private readonly Dictionary<string, List<Chirp.Chat.ChatMessage>> _channels =
            new Dictionary<string, List<Chirp.Chat.ChatMessage>>();
        private readonly Dictionary<string, HashSet<string>> _reads =
            new Dictionary<string, HashSet<string>>();

        /// <param name="path">存档文件路径;不存在则创建,存在则重放。</param>
        /// <param name="maxPerChannel">每频道内存上限,超出淘汰最旧;0 = 不设限。</param>
        public FileMessageStore(string path, int maxPerChannel = 200)
        {
            _path = path;
            _maxPerChannel = maxPerChannel;
            Replay();
        }

        public void Save(Chirp.Chat.ChatMessage message)
        {
            if (message == null) return;
            var bytes = message.ToByteArray();
            lock (_gate)
            {
                AppendRecord(KindMessage, bytes);
                var key = Key(message.ChannelType, message.ChannelId);
                if (!_channels.TryGetValue(key, out var bucket))
                {
                    bucket = new List<Chirp.Chat.ChatMessage>();
                    _channels[key] = bucket;
                }
                // Snapshot: the caller may reuse/mutate the object after Save
                // (C++ stores by value); the on-disk record is already a copy.
                bucket.Add(message.Clone());
                if (_maxPerChannel > 0 && bucket.Count > _maxPerChannel)
                {
                    bucket.RemoveAt(0); // in-memory only; Compact() retires it on disk
                }
            }
        }

        public List<Chirp.Chat.ChatMessage> Load(Chirp.Chat.ChannelType type, string channelId,
            int limit, long beforeTimestamp = 0)
        {
            var result = new List<Chirp.Chat.ChatMessage>();
            if (limit <= 0 || channelId == null) return result;
            var key = Key(type, channelId);
            lock (_gate)
            {
                if (!_channels.TryGetValue(key, out var bucket)) return result;
                for (var i = bucket.Count - 1; i >= 0 && result.Count < limit; i--)
                {
                    var message = bucket[i];
                    if (beforeTimestamp == 0 || message.Timestamp < beforeTimestamp)
                    {
                        result.Add(message);
                    }
                }
            }
            return result;
        }

        public void MarkRead(Chirp.Chat.ChannelType type, string channelId, string messageId)
        {
            if (messageId == null || messageId.Length == 0) return;
            var key = Key(type, channelId);
            lock (_gate)
            {
                if (!_reads.TryGetValue(key, out var ids))
                {
                    ids = new HashSet<string>();
                    _reads[key] = ids;
                }
                if (!ids.Add(messageId)) return; // already read: keep the log lean
                AppendRecord(KindRead, Encoding.UTF8.GetBytes(key + "\n" + messageId));
            }
        }

        public int GetUnreadCount(Chirp.Chat.ChannelType type, string channelId)
        {
            if (channelId == null) return 0;
            var key = Key(type, channelId);
            lock (_gate)
            {
                if (!_channels.TryGetValue(key, out var bucket)) return 0;
                _reads.TryGetValue(key, out var ids);
                var unread = 0;
                foreach (var message in bucket)
                {
                    // 发送侧本地存档的 messageId 为空,天然不计未读。
                    if (message.MessageId.Length > 0 && ids?.Contains(message.MessageId) != true)
                    {
                        unread++;
                    }
                }
                return unread;
            }
        }

        public void Cleanup(long olderThanTimestamp)
        {
            lock (_gate)
            {
                var removed = false;
                foreach (var bucket in _channels.Values)
                {
                    removed |= bucket.RemoveAll(m => m.Timestamp < olderThanTimestamp) > 0;
                }
                if (removed) CompactLocked();
            }
        }

        /// <summary>把当前内存快照原子重写回文件(tmp + File.Replace):
        /// 此后被淘汰/清理的条目不再随重放复活。锁内串行执行。</summary>
        public void Compact()
        {
            lock (_gate)
            {
                CompactLocked();
            }
        }

        // ---- internals ------------------------------------------------------

        private void Replay()
        {
            byte[] blob;
            try
            {
                blob = File.Exists(_path) ? File.ReadAllBytes(_path) : Array.Empty<byte>();
            }
            catch (IOException)
            {
                blob = Array.Empty<byte>();
            }

            var offset = 0;
            if (blob.Length >= Magic.Length &&
                Encoding.ASCII.GetString(blob, 0, Magic.Length) == Magic)
            {
                offset = Magic.Length;
                try
                {
                    while (offset + 5 <= blob.Length)
                    {
                        var kind = blob[offset];
                        var length = (int)ReadUInt32(blob, offset + 1);
                        if (kind != KindMessage && kind != KindRead) break;
                        if (offset + 5 + length > blob.Length) break; // truncated tail
                        var payload = new byte[length];
                        Buffer.BlockCopy(blob, offset + 5, payload, 0, length);
                        offset += 5 + length;
                        if (kind == KindMessage)
                        {
                            var message = Chirp.Chat.ChatMessage.Parser.ParseFrom(payload);
                            AddToBucket(message);
                        }
                        else
                        {
                            var line = Encoding.UTF8.GetString(payload);
                            var nl = line.IndexOf('\n');
                            if (nl <= 0) continue;
                            var key = line.Substring(0, nl);
                            var id = line.Substring(nl + 1);
                            if (!_reads.TryGetValue(key, out var ids))
                            {
                                ids = new HashSet<string>();
                                _reads[key] = ids;
                            }
                            ids.Add(id);
                        }
                    }
                }
                catch (InvalidProtocolBufferException)
                {
                    // Corrupt record: keep everything replayed before it.
                }

                // 残尾(崩溃时写了一半)就地截掉,保证后续 append 永远
                // 接在干净边界上;完整记录一字节不动。
                if (offset < blob.Length)
                {
                    TruncateTo(offset);
                }
            }

            // 坏头/空文件:铺一个新头;已识别的日志:只补缺失的头。
            if (offset < Magic.Length)
            {
                WriteHeader();
            }
        }

        private void AddToBucket(Chirp.Chat.ChatMessage message)
        {
            var key = Key(message.ChannelType, message.ChannelId);
            if (!_channels.TryGetValue(key, out var bucket))
            {
                bucket = new List<Chirp.Chat.ChatMessage>();
                _channels[key] = bucket;
            }
            bucket.Add(message);
            if (_maxPerChannel > 0 && bucket.Count > _maxPerChannel)
            {
                bucket.RemoveAt(0);
            }
        }

        private void AppendRecord(byte kind, byte[] payload)
        {
            using (var stream = new FileStream(_path, FileMode.Append, FileAccess.Write, FileShare.Read))
            {
                var head = new byte[5];
                head[0] = kind;
                WriteUInt32(head, 1, (uint)payload.Length);
                stream.Write(head, 0, head.Length);
                stream.Write(payload, 0, payload.Length);
            }
        }

        private void WriteHeader()
        {
            var dir = Path.GetDirectoryName(Path.GetFullPath(_path));
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            File.WriteAllBytes(_path, Encoding.ASCII.GetBytes(Magic));
        }

        private void TruncateTo(long length)
        {
            try
            {
                using (var stream = new FileStream(_path, FileMode.Open, FileAccess.Write))
                {
                    stream.SetLength(length);
                }
            }
            catch (IOException)
            {
                // 截不掉也不影响本次会话:内存索引已是干净前缀,残尾
                // 会在下次 Replay 时再次尝试移除。
            }
        }

        private void CompactLocked()
        {
            // 已删消息的已读标记没有存在的意义:先丢弃,再落盘快照,
            // 保证 Compact 之后文件里不再有死条目。
            foreach (var key in new List<string>(_reads.Keys))
            {
                if (!_channels.ContainsKey(key))
                {
                    _reads.Remove(key);
                }
            }
            foreach (var entry in _channels)
            {
                if (_reads.TryGetValue(entry.Key, out var ids))
                {
                    var live = new HashSet<string>();
                    foreach (var message in entry.Value)
                    {
                        if (message.MessageId.Length > 0) live.Add(message.MessageId);
                    }
                    ids.IntersectWith(live);
                }
            }

            // 先写临时文件再原子替换:重写中途崩溃也不会损坏旧档案。
            var tmp = _path + ".tmp";
            var dir = Path.GetDirectoryName(Path.GetFullPath(_path));
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            using (var stream = new FileStream(tmp, FileMode.Create, FileAccess.Write))
            {
                var head = Encoding.ASCII.GetBytes(Magic);
                stream.Write(head, 0, head.Length);
                foreach (var entry in _channels)
                {
                    foreach (var message in entry.Value)
                    {
                        WriteRecord(stream, KindMessage, message.ToByteArray());
                    }
                }
                foreach (var entry in _reads)
                {
                    foreach (var id in entry.Value)
                    {
                        WriteRecord(stream, KindRead, Encoding.UTF8.GetBytes(entry.Key + "\n" + id));
                    }
                }
            }
            File.Replace(tmp, _path, null);
        }

        private static void WriteRecord(FileStream stream, byte kind, byte[] payload)
        {
            var head = new byte[5];
            head[0] = kind;
            WriteUInt32(head, 1, (uint)payload.Length);
            stream.Write(head, 0, head.Length);
            stream.Write(payload, 0, payload.Length);
        }

        private static string Key(Chirp.Chat.ChannelType type, string channelId)
            => (int)type + "|" + channelId;

        private static uint ReadUInt32(byte[] buffer, int offset)
            => (uint)(buffer[offset] | (buffer[offset + 1] << 8) |
                      (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24));

        private static void WriteUInt32(byte[] buffer, int offset, uint value)
        {
            buffer[offset] = (byte)value;
            buffer[offset + 1] = (byte)(value >> 8);
            buffer[offset + 2] = (byte)(value >> 16);
            buffer[offset + 3] = (byte)(value >> 24);
        }
    }
}
