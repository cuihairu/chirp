using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>Hook-surface tests: the five sdks/core parity interfaces
/// (interceptor / auth provider / message store / event listener / command
/// handler) plus reply support and private-channel normalization on the
/// full-pipeline send.</summary>
public class ChirpHookTests
{
    private static Task Settle() => FakeTransport.SettleAsync();

    // ----- MemoryMessageStore -----

    private static Chirp.Chat.ChatMessage Msg(string id, string channelId, long ts,
        string content = "hi", string senderId = "peer")
    {
        return new Chirp.Chat.ChatMessage
        {
            MessageId = id,
            SenderId = senderId,
            ChannelType = Chirp.Chat.ChannelType.World,
            ChannelId = channelId,
            Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(content)),
            Timestamp = ts,
        };
    }

    [Fact]
    public void Store_LoadsNewestFirst_WithLimitAndBefore()
    {
        var store = new MemoryMessageStore();
        for (var i = 1; i <= 5; i++)
        {
            store.Save(Msg("m" + i, "w", i));
        }

        var recent = store.Load(Chirp.Chat.ChannelType.World, "w", 3);
        Assert.Equal(new[] { "m5", "m4", "m3" }, recent.Select(m => m.MessageId));

        var older = store.Load(Chirp.Chat.ChannelType.World, "w", 2, beforeTimestamp: 4);
        Assert.Equal(new[] { "m3", "m2" }, older.Select(m => m.MessageId));

        // Channels are independent buckets.
        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "other", 10));
    }

    [Fact]
    public void Store_EvictsOldestBeyondCap()
    {
        var store = new MemoryMessageStore(maxPerChannel: 3);
        for (var i = 1; i <= 4; i++)
        {
            store.Save(Msg("m" + i, "w", i));
        }
        Assert.Equal(new[] { "m4", "m3", "m2" },
            store.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));
    }

    [Fact]
    public void Store_CleanupDropsOlderThanCutoff()
    {
        var store = new MemoryMessageStore();
        store.Save(Msg("old", "w", 10));
        store.Save(Msg("new", "w", 20));
        store.Cleanup(olderThanTimestamp: 15);
        Assert.Equal(new[] { "new" },
            store.Load(Chirp.Chat.ChannelType.World, "w", 10).Select(m => m.MessageId));
    }

    [Fact]
    public void Store_UnreadDefaultsToZero_LikeCppReference()
    {
        // MarkRead/GetUnreadCount are interface defaults: reach them
        // through IMessageStore, exactly like a custom store would.
        IMessageStore store = new MemoryMessageStore();
        store.Save(Msg("m1", "w", 1));
        store.MarkRead(Chirp.Chat.ChannelType.World, "w", "m1");
        Assert.Equal(0, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "w", 0));
        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "missing", 5));
    }

    [Fact]
    public void Store_SavesSnapshot_NotCallerReference()
    {
        var store = new MemoryMessageStore();
        var msg = Msg("m1", "w", 1);
        store.Save(msg);
        msg.MessageId = "hijacked"; // caller mutates after save
        Assert.Equal("m1",
            store.Load(Chirp.Chat.ChannelType.World, "w", 10).Single().MessageId);
    }

    // ----- test scaffolding over FakeTransport -----

    private static async Task<ChirpClient> ConnectAsync(FakeTransport transport,
        ChirpClientOptions? options = null)
    {
        var client = new ChirpClient(
            "ws://test",
            transportFactory: _ => transport,
            options: options ?? new ChirpClientOptions { RequestTimeoutMs = 10_000 });
        await client.ConnectAsync();
        return client;
    }

    private static Packet RespPacket(long sequence, MsgID msgId, IMessage body)
    {
        return new Packet
        {
            MsgId = msgId,
            Sequence = sequence,
            Body = ByteString.CopyFrom(body.ToByteArray()),
        };
    }

    /// <summary>Connected + logged in through the new client-side
    /// LoginAsync (protocol round-trip).</summary>
    private static async Task<ChirpClient> LoginAsync(FakeTransport transport,
        string token = "tok", string userId = "u1")
    {
        var client = await ConnectAsync(transport);
        var pending = client.LoginAsync(userId, deviceId: "d1", token: token);
        await Settle();
        var req = transport.LastSentPacket();
        Assert.Equal(MsgID.LoginReq, req.MsgId);
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.Ok, UserId = userId }));
        await pending;
        return client;
    }

    /// <summary>Full-pipeline send with an echoed SEND_MESSAGE_RESP.</summary>
    private static async Task<Chirp.Chat.SendMessageResponse> SendEchoAsync(
        ChirpClient client, FakeTransport transport, SendOptions options, string content,
        string senderId = "u1", string serverMessageId = "srv-1")
    {
        var pending = client.SendMessageAsync(options, content, senderId);
        await Settle();
        var req = transport.LastSentPacket();
        Assert.Equal(MsgID.SendMessageReq, req.MsgId);
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.SendMessageResp,
            new Chirp.Chat.SendMessageResponse
            {
                Code = Chirp.Common.ErrorCode.Ok,
                MessageId = serverMessageId,
            }));
        return await pending;
    }

    private static void PushChat(FakeTransport transport, string messageId, string content,
        long ts = 1234)
    {
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(new Chirp.Chat.ChatMessage
            {
                MessageId = messageId,
                SenderId = "peer",
                ChannelType = Chirp.Chat.ChannelType.World,
                ChannelId = "world-0",
                Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(content)),
                Timestamp = ts,
            }.ToByteArray()),
        });
    }

    private static string TextOf(ByteString content) =>
        Encoding.UTF8.GetString(content.ToByteArray());

    private sealed class RecordingListener : IChatEventListener
    {
        public readonly List<ConnStatus> Statuses = new List<ConnStatus>();
        public readonly List<Chirp.Common.ErrorCode> LoginCodes = new List<Chirp.Common.ErrorCode>();
        public readonly List<string> LoginUsers = new List<string>();
        public readonly List<string> KickReasons = new List<string>();
        public readonly List<(int Attempt, int DelayMs)> Reconnectings = new List<(int, int)>();
        public int ReconnectedCount;
        public readonly List<Chirp.Chat.ChatMessage> Messages = new List<Chirp.Chat.ChatMessage>();

        public void OnConnectionStateChanged(ConnStatus status) => Statuses.Add(status);
        public void OnLoginResult(Chirp.Common.ErrorCode code, string userId)
        {
            LoginCodes.Add(code);
            LoginUsers.Add(userId);
        }
        public void OnKicked(string reason) => KickReasons.Add(reason);
        public void OnReconnecting(int attempt, int delayMs) =>
            Reconnectings.Add((attempt, delayMs));
        public void OnReconnected() => ReconnectedCount++;
        public void OnMessageReceived(Chirp.Chat.ChatMessage message) => Messages.Add(message);
    }

    // ----- send pipeline: interceptor / command / archive -----

    [Fact]
    public async Task InterceptorSendRewrite_ReachesWire()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        client.SetMessageInterceptor(new RewriterInterceptor(send: "clean", receive: null));

        await SendEchoAsync(client, transport, WorldOptions(), "dirty");
        Assert.Equal("clean", TextOf(SentRequest(transport).Content));
    }

    [Fact]
    public async Task InterceptorSendBlock_NeverHitsWire()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var interceptor = new BlockingInterceptor(blockSend: true);
        client.SetMessageInterceptor(interceptor);

        var error = await Assert.ThrowsAsync<RequestError>(() =>
            client.SendMessageAsync(WorldOptions(), "spam", "u1"));

        Assert.Equal(RequestErrorKind.Blocked, error.Kind);
        Assert.Empty(transport.Sent);
        Assert.False(interceptor.AfterSendFired);
    }

    [Fact]
    public async Task InterceptorAfterSend_FiresOnSuccess()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var interceptor = new RecordingInterceptor();
        client.SetMessageInterceptor(interceptor);

        await SendEchoAsync(client, transport, WorldOptions(), "hello");

        Assert.True(interceptor.AfterSendFired);
        Assert.Equal("hello", TextOf(interceptor.LastSent!.Content));
    }

    [Fact]
    public async Task ReceiveRewrite_StoreAndListenerSeeRewire_RawNotifySeesOriginal()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var store = new MemoryMessageStore();
        var listener = new RecordingListener();
        client.SetMessageStore(store);
        client.AddListener(listener);
        client.SetMessageInterceptor(new RewriterInterceptor(send: null, receive: "clean"));

        var rawBodies = new List<byte[]>();
        client.OnNotify(MsgID.ChatMessageNotify, rawBodies.Add);
        PushChat(transport, "srv-9", "dirty");
        await Settle();

        // Parsed-pipeline consumers see the rewrite...
        var archived = store.Load(Chirp.Chat.ChannelType.World, "world-0", 10);
        Assert.Single(archived);
        Assert.Equal("clean", TextOf(archived[0].Content));
        Assert.Single(listener.Messages);
        Assert.Equal("clean", TextOf(listener.Messages[0].Content));
        // ...while OnNotify subscribers still get the raw wire body.
        Assert.Equal("dirty",
            TextOf(Chirp.Chat.ChatMessage.Parser.ParseFrom(rawBodies.Single()).Content));
    }

    [Fact]
    public async Task ReceiveDrop_KillsEverythingDownstream()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var store = new MemoryMessageStore();
        var listener = new RecordingListener();
        var interceptor = new RecordingInterceptor();
        interceptor.BlockReceive = true;
        client.SetMessageStore(store);
        client.AddListener(listener);
        client.SetMessageInterceptor(interceptor);

        var rawBodies = new List<byte[]>();
        client.OnNotify(MsgID.ChatMessageNotify, rawBodies.Add);
        PushChat(transport, "srv-9", "spam");
        await Settle();

        Assert.Empty(store.Load(Chirp.Chat.ChannelType.World, "world-0", 10));
        Assert.Empty(listener.Messages);
        Assert.Empty(rawBodies);
        Assert.False(interceptor.AfterReceiveFired);
    }

    [Fact]
    public async Task SentAndReceived_BothLandInArchive()
    {
        var transport = new FakeTransport();
        var client = await LoginAsync(transport);
        client.SetMessageStore(new MemoryMessageStore());

        await SendEchoAsync(client, transport, WorldOptions(), "out", serverMessageId: "srv-out");
        PushChat(transport, "srv-in", "in");
        await Settle();

        var archived = client.LoadHistory(Chirp.Chat.ChannelType.World, "world-0", 10);
        Assert.Equal(2, archived.Count);
        // Newest first: the push landed after the send.
        Assert.Equal("srv-in", archived[0].MessageId);
        // Fire-and-forget send-side archive has no server id (read
        // resp.MessageId for it — C++ reference behavior).
        Assert.Equal("", archived[1].MessageId);
        Assert.Equal("out", TextOf(archived[1].Content));
    }

    [Fact]
    public void LoadHistory_WithoutStore_IsEmpty()
    {
        TestClient.Connected(new FakeTransport(), out var client);
        Assert.Empty(client.LoadHistory(Chirp.Chat.ChannelType.World, "w", 10));
        Assert.Equal(0, client.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
        client.MarkRead(Chirp.Chat.ChannelType.World, "w", "m1"); // must not throw
        client.CleanupMessages(999); // must not throw
    }

    // ----- '/'-command routing -----

    private sealed class EchoCommand : ICommandHandler
    {
        public string Name { get; }
        public string Description { get; }
        public string ArgsSeen = "";
        public string SenderSeen = "";
        public int Calls;

        public EchoCommand(string name, string description = "test")
        {
            Name = name;
            Description = description;
        }

        public bool Execute(string args, string senderId)
        {
            Calls++;
            ArgsSeen = args;
            SenderSeen = senderId;
            return true;
        }
    }

    private sealed class DecliningCommand : ICommandHandler
    {
        public string Name { get; }
        public string Description { get; }
        public DecliningCommand(string name) { Name = name; Description = "declines"; }
        public bool Execute(string args, string senderId) => false;
    }

    [Fact]
    public async Task CommandHit_HandledLocally_NeverHitsWire()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var trade = new EchoCommand("trade");
        client.RegisterCommand(trade);

        var error = await Assert.ThrowsAsync<RequestError>(() =>
            client.SendMessageAsync(WorldOptions(), "/trade alice 100", "u1"));

        Assert.Equal(RequestErrorKind.Blocked, error.Kind);
        Assert.Empty(transport.Sent);
        Assert.Equal(1, trade.Calls);
        Assert.Equal("alice 100", trade.ArgsSeen);
        Assert.Equal("u1", trade.SenderSeen);
        Assert.Equal("/trade", ((ICommandHandler)trade).Usage); // interface default
    }

    [Fact]
    public async Task CommandMiss_DroppedLocally()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        client.RegisterCommand(new EchoCommand("trade"));

        var error = await Assert.ThrowsAsync<RequestError>(() =>
            client.SendMessageAsync(WorldOptions(), "/dance", "u1"));

        Assert.Equal(RequestErrorKind.Blocked, error.Kind);
        Assert.Contains("unknown command", error.Message);
        Assert.Empty(transport.Sent);
    }

    [Fact]
    public async Task SlashText_PassesThrough_WithoutCommands()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);

        await SendEchoAsync(client, transport, WorldOptions(), "/dance");

        Assert.Equal("/dance", TextOf(SentRequest(transport).Content));
    }

    [Fact]
    public async Task DecliningHandler_LetsNextHandlerTry()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var trade = new EchoCommand("trade");
        client.RegisterCommand(new DecliningCommand("trade"));
        client.RegisterCommand(trade);

        await Assert.ThrowsAsync<RequestError>(() =>
            client.SendMessageAsync(WorldOptions(), "/trade x", "u1"));

        Assert.Equal(1, trade.Calls);
        Assert.Empty(transport.Sent);
    }

    // ----- send validation & reply support -----

    [Fact]
    public async Task ReplyToMessageId_ReachesWire_AndArchive()
    {
        var transport = new FakeTransport();
        var client = await LoginAsync(transport);
        client.SetMessageStore(new MemoryMessageStore());

        var options = WorldOptions();
        options.ReplyToMessageId = "m-42";
        await SendEchoAsync(client, transport, options, "a reply");

        var wire = SentRequest(transport);
        Assert.Equal("m-42", wire.ReplyToMessageId);
        var archived = client.LoadHistory(Chirp.Chat.ChannelType.World, "world-0", 10);
        Assert.Equal("m-42", archived.Single().ReplyToMessageId);
    }

    [Fact]
    public async Task PrivateChannel_NormalizesSortedPair()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);

        await SendEchoAsync(client, transport, PrivateOptions("a"), "one", senderId: "b");
        Assert.Equal("a|b", SentRequest(transport).ChannelId);

        await SendEchoAsync(client, transport, PrivateOptions("b"), "two", senderId: "a");
        Assert.Equal("a|b", SentRequest(transport).ChannelId);
    }

    [Fact]
    public async Task NonPrivate_RequiresExplicitChannelId()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var options = new SendOptions
        {
            ChannelType = Chirp.Chat.ChannelType.World,
            ReceiverId = "someone",
        };
        await Assert.ThrowsAsync<ArgumentException>(() =>
            client.SendMessageAsync(options, "hi", "u1"));
        Assert.Empty(transport.Sent);
    }

    [Fact]
    public async Task PrivateSend_RequiresReceiver()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        await Assert.ThrowsAsync<ArgumentException>(() =>
            client.SendMessageAsync(new SendOptions(), "hi", "u1"));
        Assert.Empty(transport.Sent);
    }

    [Fact]
    public async Task NotConnected_FailsFast_BeforeValidation()
    {
        TestClient.Connected(new FakeTransport(), out var client);
        var error = await Assert.ThrowsAsync<RequestError>(() =>
            client.SendMessageAsync(WorldOptions(), "", "u1"));
        Assert.Equal(RequestErrorKind.Closed, error.Kind);
    }

    private static SendOptions WorldOptions() => new SendOptions
    {
        ChannelType = Chirp.Chat.ChannelType.World,
        ChannelId = "world-0",
    };

    private static SendOptions PrivateOptions(string receiverId) => new SendOptions
    {
        ChannelType = Chirp.Chat.ChannelType.Private,
        ReceiverId = receiverId,
    };

    private static Chirp.Chat.SendMessageRequest SentRequest(FakeTransport transport)
    {
        return Chirp.Chat.SendMessageRequest.Parser.ParseFrom(
            transport.LastSentPacket().Body.ToByteArray());
    }

    private sealed class RecordingInterceptor : IMessageInterceptor
    {
        public bool BlockReceive;
        public bool AfterSendFired;
        public bool AfterReceiveFired;
        public Chirp.Chat.SendMessageRequest? LastSent;

        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request)
        {
            LastSent = request;
            return true;
        }

        public void OnAfterSend(Chirp.Chat.SendMessageRequest request) => AfterSendFired = true;

        public bool OnBeforeReceive(Chirp.Chat.ChatMessage message) => !BlockReceive;

        public void OnAfterReceive(Chirp.Chat.ChatMessage message) => AfterReceiveFired = true;
    }

    private sealed class BlockingInterceptor : IMessageInterceptor
    {
        private readonly bool _blockSend;
        public bool AfterSendFired;

        public BlockingInterceptor(bool blockSend) { _blockSend = blockSend; }

        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request) => !_blockSend;

        public void OnAfterSend(Chirp.Chat.SendMessageRequest request) => AfterSendFired = true;
    }

    /// <summary>Rewrites one direction (send and/or receive) to a fixed text.</summary>
    private sealed class RewriterInterceptor : IMessageInterceptor
    {
        private readonly string? _sendRewrite;
        private readonly string? _receiveRewrite;

        public RewriterInterceptor(string? send, string? receive)
        {
            _sendRewrite = send;
            _receiveRewrite = receive;
        }

        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request)
        {
            if (_sendRewrite != null)
            {
                request.Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(_sendRewrite));
            }
            return true;
        }

        public bool OnBeforeReceive(Chirp.Chat.ChatMessage message)
        {
            if (_receiveRewrite != null)
            {
                message.Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(_receiveRewrite));
            }
            return true;
        }
    }

    // ----- event listeners -----

    [Fact]
    public async Task Listener_SeesConnectionStateSequence()
    {
        var transport = new FakeTransport();
        TestClient.Connected(transport, out var client);
        var listener = new RecordingListener();
        client.AddListener(listener);

        await client.ConnectAsync();

        Assert.Equal(new[] { ConnStatus.Connecting, ConnStatus.Connected }, listener.Statuses);
    }

    [Fact]
    public async Task Listener_SeesLoginResult_AndProviderAuthResult()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        var provider = new RecordingProvider { Token = "tok" };
        client.AddListener(listener);
        client.SetAuthProvider(provider);

        var pending = client.LoginAsync("u1", "d1");
        await Settle();
        var req = transport.LastSentPacket();
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.Ok, UserId = "u1" }));
        await pending;

        Assert.Equal(new[] { Chirp.Common.ErrorCode.Ok }, listener.LoginCodes);
        Assert.Equal(new[] { "u1" }, listener.LoginUsers);
        Assert.Equal(new[] { Chirp.Common.ErrorCode.Ok }, provider.AuthResults);
        Assert.Equal(new[] { "u1" }, provider.AuthResultUsers);
    }

    [Fact]
    public async Task Listener_SeesKicked_WithReason()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        client.AddListener(listener);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.KickNotify,
            Body = ByteString.CopyFrom(
                new Chirp.Auth.KickNotify { Reason = "device takeover" }.ToByteArray()),
        });
        await Settle();

        Assert.Equal(new[] { "device takeover" }, listener.KickReasons);
        Assert.Contains(ConnStatus.Kicked, listener.Statuses);
    }

    [Fact]
    public async Task Listener_SeesReconnecting_OnRemoteClose()
    {
        var transport = new FakeTransport();
        // Default backoff (500ms base): the Settle window must NOT outlive
        // WaitingReconnect, so a scheduled reconnect cannot complete under us.
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        client.AddListener(listener);

        transport.SimulateRemoteClose();
        await Settle();

        Assert.Equal(ConnStatus.WaitingReconnect, client.Status);
        var attempt = Assert.Single(listener.Reconnectings);
        Assert.Equal(1, attempt.Attempt);
        Assert.InRange(attempt.DelayMs, 1, 1000);

        client.Disconnect(); // cancel the scheduled reconnect
        await Task.Delay(1200);
        Assert.Equal(ConnStatus.Closed, client.Status);
    }

    // ----- auth provider -----

    private sealed class RecordingProvider : IAuthProvider
    {
        public string Token = "tok";
        public string? RenewWith;
        public int RenewCalls;
        public readonly List<Chirp.Common.ErrorCode> AuthResults = new List<Chirp.Common.ErrorCode>();
        public readonly List<string> AuthResultUsers = new List<string>();

        public string GetToken() => Token;

        public Task<string?> RenewTokenAsync()
        {
            RenewCalls++;
            return Task.FromResult(RenewWith);
        }

        public void OnAuthResult(Chirp.Common.ErrorCode code, string userId)
        {
            AuthResults.Add(code);
            AuthResultUsers.Add(userId);
        }
    }

    [Fact]
    public async Task Login_UsesProviderToken_WhenExplicitMissing()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        client.SetAuthProvider(new RecordingProvider { Token = "jwt-xyz" });

        var pending = client.LoginAsync("u1", "d1");
        await Settle();
        var req = transport.LastSentPacket();
        Assert.Equal("jwt-xyz",
            Chirp.Auth.LoginRequest.Parser.ParseFrom(req.Body.ToByteArray()).Token);
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.Ok, UserId = "u1" }));
        await pending;
    }

    [Fact]
    public async Task Login_RenewsExactlyOnce_OnAuthFailed()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        var provider = new RecordingProvider { Token = "stale", RenewWith = "fresh" };
        client.AddListener(listener);
        client.SetAuthProvider(provider);

        var pending = client.LoginAsync("u1", "d1");
        await Settle();
        var first = transport.LastSentPacket();
        transport.ServerPacket(RespPacket(first.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.AuthFailed }));
        await Settle();

        var second = transport.LastSentPacket();
        Assert.NotEqual(first.Sequence, second.Sequence);
        transport.ServerPacket(RespPacket(second.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.Ok, UserId = "u1" }));

        await pending;

        Assert.Equal("fresh", Chirp.Auth.LoginRequest.Parser
            .ParseFrom(second.Body.ToByteArray()).Token);
        Assert.Equal(1, provider.RenewCalls);
        Assert.Equal(new[] { Chirp.Common.ErrorCode.Ok }, listener.LoginCodes);
        Assert.Equal(new[] { Chirp.Common.ErrorCode.Ok }, provider.AuthResults);
    }

    [Fact]
    public async Task Login_RenewedLoginStillFailing_IsFinal()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        var provider = new RecordingProvider { Token = "stale", RenewWith = "fresh" };
        client.AddListener(listener);
        client.SetAuthProvider(provider);

        var pending = client.LoginAsync("u1", "d1");
        for (var round = 0; round < 2; round++)
        {
            await Settle();
            var req = transport.LastSentPacket();
            transport.ServerPacket(RespPacket(req.Sequence, MsgID.LoginResp,
                new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.AuthFailed }));
            await Settle();
        }

        var error = await Assert.ThrowsAsync<RequestError>(() => pending);
        Assert.Equal(Chirp.Common.ErrorCode.AuthFailed, error.Code);
        Assert.Equal(1, provider.RenewCalls); // no second renewal
        Assert.Equal(new[] { Chirp.Common.ErrorCode.AuthFailed }, listener.LoginCodes);
        Assert.Equal(new[] { Chirp.Common.ErrorCode.AuthFailed }, provider.AuthResults);
    }

    [Fact]
    public async Task Login_ProviderDeclinesRenewal_FailsImmediately()
    {
        var transport = new FakeTransport();
        var client = await ConnectAsync(transport);
        var provider = new RecordingProvider { Token = "stale", RenewWith = null };
        client.SetAuthProvider(provider);

        var pending = client.LoginAsync("u1", "d1");
        await Settle();
        var req = transport.LastSentPacket();
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.AuthFailed }));

        var error = await Assert.ThrowsAsync<RequestError>(() => pending);
        Assert.Equal(Chirp.Common.ErrorCode.AuthFailed, error.Code);
        await Settle();
        Assert.Equal(1, provider.RenewCalls);
        // Still exactly one LOGIN_REQ on the wire.
        Assert.Single(transport.Sent);
    }
}
