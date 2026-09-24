using System;
using System.Text;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>WordFilterInterceptor:词库解析与改写/拒绝语义对齐服务端
/// chirp::chat::WordFilter;管线用例验证经 ChirpClient 的真实改写/拦截。</summary>
public class ChirpWordFilterTests
{
    private static Chirp.Chat.SendMessageRequest Req(string content)
    {
        return new Chirp.Chat.SendMessageRequest
        {
            Content = Google.Protobuf.ByteString.CopyFrom(Encoding.UTF8.GetBytes(content)),
        };
    }

    private static string TextOf(Chirp.Chat.SendMessageRequest request) =>
        request.Content.ToStringUtf8();

    private static async Task<ChirpClient> LoginAsync(FakeTransport transport)
    {
        var client = new ChirpClient(
            "ws://test",
            transportFactory: _ => transport,
            options: new ChirpClientOptions { RequestTimeoutMs = 10_000 });
        await client.ConnectAsync();
        var pending = client.LoginAsync("u1", deviceId: "d1", token: "tok");
        await FakeTransport.SettleAsync();
        var request = transport.LastSentPacket();
        Assert.Equal(MsgID.LoginReq, request.MsgId);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.LoginResp,
            Sequence = request.Sequence,
            Body = Google.Protobuf.ByteString.CopyFrom(new Chirp.Auth.LoginResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                UserId = "u1",
            }.ToByteArray()),
        });
        await pending;
        return client;
    }

    [Fact]
    public void ParseLexicon_DropsBlanksCommentsAndDedupes()
    {
        var terms = WordFilterInterceptor.ParseLexicon(new[]
        {
            "  Spam  ",
            "# 注释行",
            "",
            "spam",
            "  dummy\r\n",
            "论坛",
        });

        Assert.Equal(new[] { "dummy", "spam", "论坛" }, terms);
    }

    [Fact]
    public void Replace_MasksHitsAndKeepsSurroundingCasing()
    {
        var filter = new WordFilterInterceptor(new WordFilterOptions { Terms = new[] { "bad dog" } });

        var message = Req("Bad DOG and bad dog");
        Assert.True(filter.OnBeforeSend(message));
        Assert.Equal("** and **", TextOf(message));
        Assert.Equal(1, filter.WordCount);
    }

    [Fact]
    public void Replace_CollapsesAdjacentTermRuns()
    {
        var filter = new WordFilterInterceptor(new WordFilterOptions
        {
            Terms = new[] { "ab", "bc" },
            Replacement = "#",
        });

        var message = Req("abc");
        Assert.True(filter.OnBeforeSend(message));
        // 两个词的命中区间首尾相接,重建时塌缩成一次替换。
        Assert.Equal("#", TextOf(message));
    }

    [Fact]
    public void Replace_KeepsUtf8BytesAroundAsciiHits()
    {
        var filter = new WordFilterInterceptor(new WordFilterOptions
        {
            Terms = new[] { "脏话", "damn" },
        });

        var message = Req("你好 damn 世界,真是脏话啊");
        Assert.True(filter.OnBeforeSend(message));
        Assert.Equal("你好 ** 世界,真是**啊", TextOf(message));
    }

    [Fact]
    public void Reject_BlocksHitAndPassesCleanText()
    {
        var filter = new WordFilterInterceptor(new WordFilterOptions
        {
            Terms = new[] { "banned" },
            Policy = WordFilterPolicy.Reject,
        });

        var hit = Req("totally BANNED words");
        Assert.False(filter.OnBeforeSend(hit));

        var clean = Req("perfectly fine");
        Assert.True(filter.OnBeforeSend(clean));
        Assert.Equal("perfectly fine", TextOf(clean));
    }

    [Fact]
    public void EmptyLexicon_IsANoOp()
    {
        var filter = new WordFilterInterceptor(new WordFilterOptions());
        var message = Req("anything at all");
        Assert.True(filter.OnBeforeSend(message));
        Assert.Equal("anything at all", TextOf(message));
    }

    [Fact]
    public async Task Pipeline_RewritesContentBeforeWireAndArchive()
    {
        var transport = new FakeTransport();
        var client = await LoginAsync(transport);
        client.SetMessageInterceptor(new WordFilterInterceptor(
            new WordFilterOptions { Terms = WordFilterInterceptor.ParseLexicon(new[] { "damn" }) }));
        var store = new MemoryMessageStore();
        client.SetMessageStore(store);

        var pending = client.SendMessageAsync(
            new SendOptions { ReceiverId = "alice" }, "well damn, hi", senderId: "u1");
        await FakeTransport.SettleAsync();
        var packet = transport.LastSentPacket();
        Assert.Equal(MsgID.SendMessageReq, packet.MsgId);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.SendMessageResp,
            Sequence = packet.Sequence,
            Body = Google.Protobuf.ByteString.CopyFrom(new Chirp.Chat.SendMessageResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                MessageId = "srv-9",
            }.ToByteArray()),
        });
        await pending;

        var request = Chirp.Chat.SendMessageRequest.Parser.ParseFrom(packet.Body);
        Assert.Equal("well **, hi", request.Content.ToStringUtf8());
        var local = store.Load(Chirp.Chat.ChannelType.Private, "alice|u1", 10);
        Assert.NotEmpty(local);
        Assert.Equal("well **, hi", local[0].Content.ToStringUtf8());
    }

    [Fact]
    public async Task Pipeline_RejectStopsSendWithoutServerRoundTrip()
    {
        var transport = new FakeTransport();
        var client = await LoginAsync(transport);
        client.SetMessageInterceptor(new WordFilterInterceptor(
            new WordFilterOptions
            {
                Terms = new[] { "banned" },
                Policy = WordFilterPolicy.Reject,
            }));

        var error = await Assert.ThrowsAsync<RequestError>(() => client.SendMessageAsync(
            new SendOptions { ReceiverId = "alice" }, "banned goods", senderId: "u1"));
        Assert.Equal(RequestErrorKind.Blocked, error.Kind);
        Assert.Single(transport.Sent); // 只有 LOGIN_REQ,零 SEND_MESSAGE_REQ
    }
}
