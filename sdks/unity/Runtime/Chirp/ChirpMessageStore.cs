using System;
using System.Collections.Generic;

namespace Chirp.Sdk
{
    /// <summary>内存版本地存档(对齐 C++ 参考实现的 MemoryMessageStore):
    /// 按 (channelType, channelId) 分桶尾插,超限淘汰最旧;Load 从尾部
    /// 回收,天然 newest-first。不跟踪已读——MarkRead 是 no-op,
    /// GetUnreadCount 恒 0(继承接口默认)。全部方法加锁,任意线程可调。</summary>
    public sealed class MemoryMessageStore : IMessageStore
    {
        private readonly object _gate = new object();
        private readonly int _maxPerChannel;
        private readonly Dictionary<string, List<Chirp.Chat.ChatMessage>> _channels =
            new Dictionary<string, List<Chirp.Chat.ChatMessage>>();

        /// <param name="maxPerChannel">每频道上限,超出淘汰最旧;0 = 不设限。</param>
        public MemoryMessageStore(int maxPerChannel = 200)
        {
            _maxPerChannel = maxPerChannel;
        }

        public void Save(Chirp.Chat.ChatMessage message)
        {
            if (message == null) return;
            var key = Key(message.ChannelType, message.ChannelId);
            lock (_gate)
            {
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

        public void Cleanup(long olderThanTimestamp)
        {
            lock (_gate)
            {
                foreach (var bucket in _channels.Values)
                {
                    bucket.RemoveAll(m => m.Timestamp < olderThanTimestamp);
                }
            }
        }

        private static string Key(Chirp.Chat.ChannelType type, string channelId)
            => (int)type + "|" + channelId;
    }
}
