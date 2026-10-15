using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>消息压缩消费面：gzip 原语 + GET_HISTORY 双形态拆封
/// (game_chat_features P2)。镜像 C++/TS 三端同形语义。</summary>
public class ChirpGzipTests
{
    private static Task Settle() => FakeTransport.SettleAsync();

    // ----- primitive -----

    private static byte[] Gzip(byte[] data)
    {
        using var output = new MemoryStream();
        using (var gzip = new GZipStream(output, CompressionMode.Compress, leaveOpen: true))
        {
            gzip.Write(data, 0, data.Length);
        }
        return output.ToArray();
    }

    [Fact]
    public void Inflate_RoundTrip()
    {
        var payload = new byte[4096];
        for (var i = 0; i < payload.Length; i++) payload[i] = (byte)(i * 31 + i / 7);

        var inflated = GzipInflate.Inflate(Gzip(payload));

        Assert.Equal(payload, inflated);
    }

    [Fact]
    public void Inflate_RoundTrip_LargeSpansReadChunks()
    {
        // 300k high-entropy bytes force many 8 KiB reads and a large container,
        // so a mid-stream byte flip lands inside the deflate body.
        var payload = new byte[300_000];
        var state = 0x12345678u;
        for (var i = 0; i < payload.Length; i++)
        {
            state = state * 1103515245u + 12345u;
            payload[i] = (byte)(state >> 24);
        }

        var inflated = GzipInflate.Inflate(Gzip(payload));

        Assert.Equal(payload, inflated);
    }

    [Fact]
    public void Inflate_EmptyInput_Throws()
    {
        Assert.Throws<GzipInflateError>(() => GzipInflate.Inflate(Array.Empty<byte>()));
    }

    [Fact]
    public void Inflate_CorruptContainer_Throws()
    {
        var container = Gzip(new byte[300_000]);
        container[container.Length / 2] ^= 0xFF;

        Assert.Throws<GzipInflateError>(() => GzipInflate.Inflate(container));
    }

    [Fact]
    public void Inflate_TruncatedContainer_Throws()
    {
        var container = Gzip(new byte[300_000]);
        var truncated = container.Take(container.Length / 2).ToArray();

        Assert.Throws<GzipInflateError>(() => GzipInflate.Inflate(truncated));
    }

    [Fact]
    public void Inflate_CorruptTrailer_Throws()
    {
        // The deflate body is intact; only the CRC trailer is damaged. The
        // round trip must fail rather than hand up a list the server never sent.
        var container = Gzip(new byte[1024]);
        container[container.Length - 5] ^= 0xFF;

        Assert.Throws<GzipInflateError>(() => GzipInflate.Inflate(container));
    }

    [Fact]
    public void Inflate_HeaderOnlyContainer_Throws()
    {
        // RFC 1952 header with no deflate body and no trailer.
        var container = new byte[] { 0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 };

        Assert.Throws<GzipInflateError>(() => GzipInflate.Inflate(container));
    }

    [Fact]
    public void Inflate_OverCap_Throws()
    {
        // 17 MiB of zeros compresses to a few KiB; inflation must abort at the
        // 16 MiB cap instead of buffering the whole body.
        var container = Gzip(new byte[17 * 1024 * 1024]);

        Assert.Throws<GzipInflateError>(() => GzipInflate.Inflate(container));
    }

    // ----- client surface -----

    [Fact]
    public async Task Login_DeclaresGzipCapability()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.LoginAsync("u1", "d1", token: "tok");
        await Settle();
        var login = Chirp.Auth.LoginRequest.Parser.ParseFrom(
            transport.LastSentPacket().Body.ToByteArray());
        Assert.True(login.SupportsMessageAck);
        Assert.True(login.SupportsGzip);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.LoginResp,
            Sequence = transport.LastSentPacket().Sequence,
            Body = ByteString.CopyFrom(new Chirp.Auth.LoginResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
            }.ToByteArray()),
        });

        await pending;
    }

    [Fact]
    public async Task GetHistory_InflatesGzipForm()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var inner = new Chirp.Chat.GetHistoryResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            HasMore = true,
            Messages =
            {
                new Chirp.Chat.ChatMessage { MessageId = "m1", SenderId = "u1" },
                new Chirp.Chat.ChatMessage { MessageId = "m2", SenderId = "u2" },
            },
        };
        var outer = new Chirp.Chat.GetHistoryResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            HasMore = true,
            MessagesGzip = ByteString.CopyFrom(Gzip(inner.ToByteArray())),
        };

        var pending = client.RequestAsync(
            Specs.GetHistory, new Chirp.Chat.GetHistoryRequest { UserId = "u1" });
        await Settle();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GetHistoryResp,
            Sequence = transport.LastSentPacket().Sequence,
            Body = ByteString.CopyFrom(outer.ToByteArray()),
        });

        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.True(resp.HasMore);
        Assert.Equal(0, resp.MessagesGzip.Length);
        var messages = resp.Messages.ToArray();
        Assert.Equal(2, messages.Length);
        Assert.Equal("m1", messages[0].MessageId);
        Assert.Equal("u1", messages[0].SenderId);
        Assert.Equal("m2", messages[1].MessageId);
        Assert.Equal("u2", messages[1].SenderId);
    }

    [Fact]
    public async Task GetHistory_AcceptsPlainForm()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var pending = client.RequestAsync(
            Specs.GetHistory, new Chirp.Chat.GetHistoryRequest { UserId = "u1" });
        await Settle();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GetHistoryResp,
            Sequence = transport.LastSentPacket().Sequence,
            Body = ByteString.CopyFrom(new Chirp.Chat.GetHistoryResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                HasMore = false,
                Messages =
                {
                    new Chirp.Chat.ChatMessage { MessageId = "m9", SenderId = "u9" },
                },
            }.ToByteArray()),
        });

        var resp = await pending;
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        Assert.False(resp.HasMore);
        var messages = resp.Messages.ToArray();
        Assert.Single(messages);
        Assert.Equal("m9", messages[0].MessageId);
    }

    [Fact]
    public async Task GetHistory_CorruptGzip_Throws()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        var container = Gzip(new byte[300_000]);
        container[container.Length / 2] ^= 0xFF;
        var outer = new Chirp.Chat.GetHistoryResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            MessagesGzip = ByteString.CopyFrom(container),
        };

        var pending = client.RequestAsync(
            Specs.GetHistory, new Chirp.Chat.GetHistoryRequest { UserId = "u1" });
        await Settle();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GetHistoryResp,
            Sequence = transport.LastSentPacket().Sequence,
            Body = ByteString.CopyFrom(outer.ToByteArray()),
        });

        await Assert.ThrowsAsync<GzipInflateError>(() => pending);
    }

    [Fact]
    public async Task GetHistory_GarbageGzipPayload_Throws()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        await client.ConnectAsync();

        // A valid gzip container whose payload is not a GetHistoryResponse:
        // proto parsing must fail, which is the same BadResponse-equivalent
        // outcome the C++ core reports for the identical case.
        var outer = new Chirp.Chat.GetHistoryResponse
        {
            Code = Chirp.Common.ErrorCode.Ok,
            MessagesGzip = ByteString.CopyFrom(
                Gzip(new byte[] { 0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x11, 0x22, 0x33 })),
        };

        var pending = client.RequestAsync(
            Specs.GetHistory, new Chirp.Chat.GetHistoryRequest { UserId = "u1" });
        await Settle();
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.GetHistoryResp,
            Sequence = transport.LastSentPacket().Sequence,
            Body = ByteString.CopyFrom(outer.ToByteArray()),
        });

        await Assert.ThrowsAsync<InvalidProtocolBufferException>(() => pending);
    }
}
