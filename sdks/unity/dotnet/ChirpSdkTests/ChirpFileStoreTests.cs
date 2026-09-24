using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Chirp.Sdk;
using Xunit;

/// <summary>FileMessageStore: append-only persistence over the same
/// bucket semantics as MemoryMessageStore, plus real read tracking.
/// Every test runs against a fresh temp directory.</summary>
public class ChirpFileStoreTests
{
    private static string TempPath()
    {
        var dir = Path.Combine(Path.GetTempPath(), "chirp-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, "archive.log");
    }

    private static Chirp.Chat.ChatMessage Msg(string id, string channelId, long ts,
        string content = "hi", string senderId = "peer")
    {
        return new Chirp.Chat.ChatMessage
        {
            MessageId = id,
            SenderId = senderId,
            ChannelType = Chirp.Chat.ChannelType.World,
            ChannelId = channelId,
            Content = Google.Protobuf.ByteString.CopyFrom(Encoding.UTF8.GetBytes(content)),
            Timestamp = ts,
        };
    }

    [Fact]
    public void Store_LoadsNewestFirst_WithLimitAndBefore()
    {
        var store = new FileMessageStore(TempPath());
        for (var i = 1; i <= 5; i++)
        {
            store.Save(Msg("m" + i, "w", i));
        }

        Assert.Equal(new[] { "m5", "m4", "m3" },
            store.Load(Chirp.Chat.ChannelType.World, "w", 3).Select(m => m.MessageId));
        Assert.Equal(new[] { "m3", "m2" },
            store.Load(Chirp.Chat.ChannelType.World, "w", 2, beforeTimestamp: 4)
                .Select(m => m.MessageId));
        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "missing", 5));
        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "w", 0));
    }

    [Fact]
    public void Store_EvictsOldestBeyondCap_InMemoryOnly()
    {
        var path = TempPath();
        var store = new FileMessageStore(path, maxPerChannel: 3);
        for (var i = 1; i <= 4; i++)
        {
            store.Save(Msg("m" + i, "w", i));
        }
        Assert.Equal(new[] { "m4", "m3", "m2" },
            store.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));

        // 超限淘汰不落盘:重放会复活被淘汰的条目。
        var revived = new FileMessageStore(path, maxPerChannel: 0);
        Assert.Equal(new[] { "m4", "m3", "m2", "m1" },
            revived.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));

        // Compact(带上限实例:重放时 m1 先在内存里被淘汰)之后不再复活。
        var capped = new FileMessageStore(path, maxPerChannel: 3);
        capped.Compact();
        var afterCompact = new FileMessageStore(path, maxPerChannel: 0);
        Assert.Equal(new[] { "m4", "m3", "m2" },
            afterCompact.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));
    }

    [Fact]
    public void Store_PersistsAcrossInstances()
    {
        var path = TempPath();
        var first = new FileMessageStore(path);
        first.Save(Msg("a", "w", 1, content: "first"));
        first.Save(Msg("b", "team", 2, content: "second"));

        var second = new FileMessageStore(path);
        Assert.Equal(new[] { "a" },
            second.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));
        var teamMessage = second.Load(Chirp.Chat.ChannelType.World, "team", 10).Single();
        Assert.Equal("b", teamMessage.MessageId);
        Assert.Equal("second", teamMessage.Content.ToStringUtf8());
    }

    [Fact]
    public void Store_TracksUnread_AndPersistsTheCursor()
    {
        var path = TempPath();
        var store = new FileMessageStore(path);
        store.Save(Msg("m1", "w", 1));               // server-delivered: unread
        store.Save(new Chirp.Chat.ChatMessage        // local send echo: empty id, never unread
        {
            SenderId = "u1",
            ChannelType = Chirp.Chat.ChannelType.World,
            ChannelId = "w",
            Content = Google.Protobuf.ByteString.CopyFrom(Encoding.UTF8.GetBytes("out")),
            Timestamp = 2,
        });
        store.Save(Msg("m3", "w", 3));

        Assert.Equal(2, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
        store.MarkRead(Chirp.Chat.ChannelType.World, "w", "m1");
        Assert.Equal(1, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
        store.MarkRead(Chirp.Chat.ChannelType.World, "w", "m1"); // idempotent
        Assert.Equal(1, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));

        // 重放后未读游标保持。
        var reopened = new FileMessageStore(path);
        Assert.Equal(1, reopened.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
        reopened.MarkRead(Chirp.Chat.ChannelType.World, "w", "m3");
        Assert.Equal(0, reopened.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));

        // 已读标记在 Compact 后依然存活。
        reopened.Compact();
        Assert.Equal(0, new FileMessageStore(path).GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
    }

    [Fact]
    public void Store_CleanupRewrites_SoDroppedEntriesStayDropped()
    {
        var path = TempPath();
        var store = new FileMessageStore(path);
        store.Save(Msg("old", "w", 10));
        store.Save(Msg("keep", "w", 20));
        store.MarkRead(Chirp.Chat.ChannelType.World, "w", "old");

        store.Cleanup(olderThanTimestamp: 15);

        Assert.Equal(new[] { "keep" },
            store.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));
        Assert.Equal(1, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w")); // keep 未读
        store.MarkRead(Chirp.Chat.ChannelType.World, "w", "keep");
        Assert.Equal(0, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));

        var reopened = new FileMessageStore(path);
        Assert.Equal(new[] { "keep" },
            reopened.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));
        Assert.Equal(0, reopened.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
    }

    [Fact]
    public void Store_ToleratesATruncatedTail()
    {
        var path = TempPath();
        var store = new FileMessageStore(path);
        store.Save(Msg("m1", "w", 1));
        store.Save(Msg("m2", "w", 2));

        // 模拟崩溃时写了一半:砍掉文件尾部若干字节。
        var blob = File.ReadAllBytes(path);
        File.WriteAllBytes(path, blob.Take(blob.Length - 3).ToArray());

        var reopened = new FileMessageStore(path);
        Assert.Equal(new[] { "m1" },
            reopened.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));

        // 截断后依然可继续追加。
        reopened.Save(Msg("m3", "w", 3));
        Assert.Equal(new[] { "m3", "m1" },
            new FileMessageStore(path).Load(Chirp.Chat.ChannelType.World, "w", 10)
                .Select(m => m.MessageId));
    }

    [Fact]
    public void Store_ResetsAnUnrecognizableFile_InsteadOfThrowing()
    {
        var path = TempPath();
        File.WriteAllBytes(path, Encoding.ASCII.GetBytes("some other format\n"));

        var store = new FileMessageStore(path);
        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "w", 10));

        store.Save(Msg("m1", "w", 1));
        Assert.Equal(new[] { "m1" },
            new FileMessageStore(path).Load(Chirp.Chat.ChannelType.World, "w", 10)
                .Select(m => m.MessageId));
    }
}
