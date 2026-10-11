using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>词库下发同步（WordFilterSync）的协议面测试：真实 ChirpClient +
/// FakeTransport，按线格式应答 FETCH_RESP / 投递 UPDATE_NOTIFY。11 条向量与
/// Android WordFilterSyncTest / Swift WordFilterSyncTests 同名同组（词库批次
/// 四端一致的面，此前 C# 侧缺席）。</summary>
public class ChirpWordFilterSyncTests
{
    private static Task Settle() => FakeTransport.SettleAsync();

    private static Chirp.Chat.SendMessageRequest Req(string content) => new()
    {
        Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(content)),
    };

    private static string TextOf(Chirp.Chat.SendMessageRequest request) =>
        request.Content.ToStringUtf8();

    private static Chirp.Chat.WordFilterLexicon Lexicon(
        long version, bool enabled,
        Chirp.Chat.WordFilterDeliveryPolicy policy, string text) => new()
    {
        Version = version,
        Enabled = enabled,
        Policy = policy,
        Replacement = "**",
        Lexicon = text,
    };

    // Kotlin 版断言形如 `assertEquals(true to "hello **", run(sync, "hello spam"))`
    // （通过/改写后文本）；C# 拦截器面是 bool（true=放行），拆成两条断言。
    private static void PassesAs(WordFilterSync sync, string content, string expected)
    {
        var request = Req(content);
        Assert.True(sync.OnBeforeSend(request));
        Assert.Equal(expected, TextOf(request));
    }

    private static void Blocks(WordFilterSync sync, string content)
    {
        Assert.False(sync.OnBeforeSend(Req(content)));
    }

    // ---- fetch -------------------------------------------------------------

    [Fact]
    public async Task Fetch_SendsKnownVersion_AppliesServerLexicon()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);

        var fetch = sync.FetchAsync();
        await Settle();
        var request = transport.LastSentPacket();
        Assert.Equal(MsgID.WordFilterFetchReq, request.MsgId);
        var fetchReq = Chirp.Chat.WordFilterFetchRequest.Parser.ParseFrom(request.Body);
        Assert.Equal(0, fetchReq.KnownVersion);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterFetchResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterFetchResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                Lexicon = Lexicon(3, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "spam\nbad\n"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.True(await fetch);
        Assert.Equal(3, sync.Version);
        Assert.Equal(2, sync.WordCount);
        PassesAs(sync, "hello spam", "hello **");
    }

    [Fact]
    public async Task ConditionalGet_KnownVersionMatch_SkipsRebuild()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);

        var first = sync.FetchAsync();
        await Settle();
        var firstReq = transport.LastSentPacket();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterFetchResp,
            Sequence = firstReq.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterFetchResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                Lexicon = Lexicon(3, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "spam"),
            }.ToByteArray()),
        });
        await Settle();
        Assert.True(await first);

        // 第二轮：同版本应答不带文本（条件 GET 命中），预检不动。
        var second = sync.FetchAsync();
        await Settle();
        var secondReq = transport.LastSentPacket();
        Assert.Equal(3,
            Chirp.Chat.WordFilterFetchRequest.Parser.ParseFrom(secondReq.Body).KnownVersion);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterFetchResp,
            Sequence = secondReq.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterFetchResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                Lexicon = Lexicon(3, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, ""),
            }.ToByteArray()),
        });
        await Settle();

        Assert.True(await second);
        Assert.Equal(3, sync.Version);
        Assert.Equal(1, sync.WordCount);
        PassesAs(sync, "hello spam", "hello **");
    }

    [Fact]
    public async Task NonOkFetchCode_ResolvesFalse_AppliesNothing()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);

        var fetch = sync.FetchAsync();
        await Settle();
        var request = transport.LastSentPacket();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterFetchResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterFetchResponse
            {
                Code = Chirp.Common.ErrorCode.AuthFailed,
            }.ToByteArray()),
        });
        await Settle();

        Assert.False(await fetch);
        Assert.Equal(0, sync.Version);
        Assert.Equal(0, sync.WordCount);
    }

    [Fact]
    public async Task FetchTimeout_ResolvesFalse_WithoutThrowing()
    {
        var transport = new FakeTransport();
        var client = new ChirpClient(
            "ws://test",
            transportFactory: _ => transport,
            options: new ChirpClientOptions { RequestTimeoutMs = 50 });
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);

        Assert.False(await sync.FetchAsync());
        Assert.Equal(0, sync.Version);
        Assert.Equal(0, sync.WordCount);
    }

    // ---- UPDATE_NOTIFY -----------------------------------------------------

    [Fact]
    public async Task Notify_HotSwapsThePrecheck()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);
        sync.Start();

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterUpdateNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterUpdateNotify
            {
                Lexicon = Lexicon(4, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "evil"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.Equal(4, sync.Version);
        PassesAs(sync, "you evil one", "you ** one");
    }

    [Fact]
    public async Task StaleNotifies_AreIgnored()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);
        sync.Start();

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterUpdateNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterUpdateNotify
            {
                Lexicon = Lexicon(5, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "fresh"),
            }.ToByteArray()),
        });
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterUpdateNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterUpdateNotify
            {
                Lexicon = Lexicon(2, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "stale"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.Equal(5, sync.Version);
        PassesAs(sync, "fresh", "**");
        PassesAs(sync, "stale", "stale");
    }

    [Fact]
    public async Task ServerWithoutLexicon_ClearsEvenTheLocalFallback()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);
        sync.LoadLocal(new[] { "spam" });
        PassesAs(sync, "hello spam", "hello **");

        sync.Start();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterUpdateNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterUpdateNotify
            {
                Lexicon = Lexicon(1, false,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, ""),
            }.ToByteArray()),
        });
        await Settle();

        Assert.Equal(1, sync.Version);
        Assert.Equal(0, sync.WordCount);
        PassesAs(sync, "hello spam", "hello spam");
    }

    [Fact]
    public async Task RecordPolicy_PassesThrough_WithoutPrecheck()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);
        sync.Start();

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterUpdateNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterUpdateNotify
            {
                Lexicon = Lexicon(2, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyRecord, "spam"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.Equal(0, sync.WordCount);
        PassesAs(sync, "hello spam", "hello spam");
    }

    [Fact]
    public async Task Stop_DetachesTheNotifySubscription()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);
        sync.Start();
        sync.Stop();

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterUpdateNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterUpdateNotify
            {
                Lexicon = Lexicon(9, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "late"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.Equal(0, sync.Version);
    }

    // ---- fallback & policy mirror -------------------------------------------

    [Fact]
    public async Task LoadLocal_IsFallback_UntilServerLexiconSupersedes()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);
        sync.LoadLocal(new[] { "localbad" }, WordFilterPolicy.Reject);
        Blocks(sync, "x localbad");

        var fetch = sync.FetchAsync();
        await Settle();
        var request = transport.LastSentPacket();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterFetchResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterFetchResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                Lexicon = Lexicon(1, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReplace, "serverbad"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.True(await fetch);
        PassesAs(sync, "x localbad", "x localbad");
        PassesAs(sync, "x serverbad", "x **");
    }

    [Fact]
    public async Task RejectPolicy_BlocksTheSend_BeforeTheWire()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();
        var sync = new WordFilterSync(client);

        var fetch = sync.FetchAsync();
        await Settle();
        var request = transport.LastSentPacket();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.WordFilterFetchResp,
            Sequence = request.Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.WordFilterFetchResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                Lexicon = Lexicon(1, true,
                    Chirp.Chat.WordFilterDeliveryPolicy.WordFilterPolicyReject, "spam"),
            }.ToByteArray()),
        });
        await Settle();

        Assert.True(await fetch);
        Blocks(sync, "hello spam");
        PassesAs(sync, "clean", "clean");
    }
}
