using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Chirp.Gateway;
using Chirp.Sdk;
using Google.Protobuf;
using Xunit;

/// <summary>Edge-arm coverage for ChirpClient (argument validation, hook
/// fault containment, open/close paths the happy-path suite never drives),
/// the hook interfaces' default members, the Chinese error-copy table, and
/// the codec/spec accessors. Drives a scriptable transport so failures can
/// be injected at exact pipeline points.</summary>
public class ChirpClientEdgeTests
{
    private static Task Settle() => FakeTransport.SettleAsync();

    private static void WaitUntil(Func<bool> cond, string what, int timeoutMs = 5000)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (Environment.TickCount64 < deadline)
        {
            if (cond())
            {
                return;
            }
            Thread.Sleep(10);
        }
        Assert.Fail($"timed out waiting for {what}");
    }

    // ----- scaffolding ------------------------------------------------------

    private static async Task<ChirpClient> ConnectAsync(ScriptedTransport transport,
        ChirpClientOptions? options = null)
    {
        var client = new ChirpClient("ws://test", _ => transport,
            options ?? new ChirpClientOptions { RequestTimeoutMs = 10_000 });
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

    /// <summary>Full-pipeline send with an echoed SEND_MESSAGE_RESP.</summary>
    private static async Task<Chirp.Chat.SendMessageResponse> EchoAsync(
        ChirpClient client, ScriptedTransport transport, SendOptions options,
        string content, string senderId = "u1")
    {
        var pending = client.SendMessageAsync(options, content, senderId);
        await Settle();
        var req = transport.LastSentPacket();
        Assert.Equal(MsgID.SendMessageReq, req.MsgId);
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.SendMessageResp,
            new Chirp.Chat.SendMessageResponse { Code = Chirp.Common.ErrorCode.Ok }));
        return await pending;
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
            Content = ByteString.CopyFrom(Encoding.UTF8.GetBytes(content)),
            Timestamp = ts,
        };
    }

    // ----- registry add/remove + listener fault containment -----------------

    [Fact]
    public async Task StatusChanged_ThrowingListener_DoesNotStarveOthers_RemoveWorks()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var seen = new List<ConnStatus>();
        client.StatusChanged += _ => throw new InvalidOperationException("bad listener");
        client.StatusChanged += seen.Add;
        Action<ConnStatus> noop = _ => { };
        client.StatusChanged += noop;
        client.StatusChanged -= noop; // remove arm: the noop must never fire

        client.Disconnect();
        // The throwing listener was swallowed; the healthy one saw the flip.
        Assert.Contains(ConnStatus.Closed, seen);
    }

    [Fact]
    public async Task EventListener_Throwing_DoesNotStarveOthers()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var seen = new List<ConnStatus>();
        client.AddListener(new ThrowingEventListener());
        client.AddListener(new RecordingStatusListener(seen));

        client.Disconnect();
        Assert.Contains(ConnStatus.Closed, seen);
    }

    [Fact]
    public async Task RemoveListener_And_UnregisterCommand_ReportMembership()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingStatusListener(new List<ConnStatus>());
        client.AddListener(listener);
        Assert.True(client.RemoveListener(listener));
        Assert.False(client.RemoveListener(listener)); // already gone

        var command = new RollCommand();
        client.RegisterCommand(command);
        Assert.True(client.UnregisterCommand(command));
        Assert.False(client.UnregisterCommand(command));

        // With zero handlers registered, '/' texts pass through as ordinary
        // messages instead of being consumed locally.
        var resp = await EchoAsync(client, transport, new SendOptions
        {
            ChannelType = Chirp.Chat.ChannelType.World,
            ChannelId = "world-0",
        }, "/roll 100");
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        client.Dispose();
    }

    // ----- send pipeline argument validation --------------------------------

    [Fact]
    public async Task SendMessageAsync_ValidatesArguments_WhenConnected()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var options = new SendOptions { ReceiverId = "peer" };

        await Assert.ThrowsAsync<ArgumentNullException>(
            () => client.SendMessageAsync(null!, "x", "u1"));
        await Assert.ThrowsAsync<ArgumentException>(
            () => client.SendMessageAsync(options, "", "u1"));
        await Assert.ThrowsAsync<ArgumentException>(
            () => client.SendMessageAsync(options, "x", null!));
        client.Dispose();
    }

    // ----- login token-renewal faults ---------------------------------------

    [Fact]
    public async Task LoginAsync_ThrowingRenew_MeansNoRenewal_ThrowingAuthResultContained()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var provider = new FaultyProvider();
        var listener = new RecordingListener();
        client.SetAuthProvider(provider);
        client.AddListener(listener);

        var pending = client.LoginAsync("u1", "d1");
        await Settle();
        var req = transport.LastSentPacket();
        Assert.Equal(MsgID.LoginReq, req.MsgId);
        transport.ServerPacket(RespPacket(req.Sequence, MsgID.LoginResp,
            new Chirp.Auth.LoginResponse { Code = Chirp.Common.ErrorCode.AuthFailed }));

        var err = await Assert.ThrowsAsync<RequestError>(() => pending);
        Assert.Equal(RequestErrorKind.Server, err.Kind);
        Assert.Equal(Chirp.Common.ErrorCode.AuthFailed, err.Code);
        // Renewal threw: treated as "no renewal", so exactly one login went
        // on the wire; the outcome still reached both listener and provider.
        Assert.Single(transport.Sent);
        Assert.Equal(new[] { Chirp.Common.ErrorCode.AuthFailed }, listener.LoginCodes.ToArray());
        Assert.Equal(1, provider.AuthResults);
        client.Dispose();
    }

    // ----- send pipeline hook faults ----------------------------------------

    [Fact]
    public async Task SendMessage_PipelineHookFaults_AreContained()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var options = new SendOptions { ReceiverId = "peer" };

        // A throwing OnBeforeSend blocks the send like a returning-false one.
        client.SetMessageInterceptor(new ThrowingBeforeSendInterceptor());
        var blocked = await Assert.ThrowsAsync<RequestError>(
            () => client.SendMessageAsync(options, "x", "u1"));
        Assert.Equal(RequestErrorKind.Blocked, blocked.Kind);
        Assert.Empty(transport.Sent);

        // A throwing store.Save must not take the send down with it.
        client.SetMessageInterceptor(null!);
        client.SetMessageStore(new ThrowingStore());
        var survived = await EchoAsync(client, transport, options, "past a broken store");
        Assert.Equal(Chirp.Common.ErrorCode.Ok, survived.Code);

        // A throwing OnAfterSend must not mask the response either.
        client.SetMessageInterceptor(new ThrowingAfterSendInterceptor());
        client.SetMessageStore(new MemoryMessageStore());
        var again = await EchoAsync(client, transport, options, "past a broken hook");
        Assert.Equal(Chirp.Common.ErrorCode.Ok, again.Code);
        client.Dispose();
    }

    [Fact]
    public async Task CommandHandler_Throwing_Declines_LikeAMiss()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        client.RegisterCommand(new ThrowingCommand());

        // The throwing handler declines; no other handler matches, so the
        // message is dropped locally as an unknown command.
        var err = await Assert.ThrowsAsync<RequestError>(
            () => client.SendMessageAsync(new SendOptions { ReceiverId = "peer" },
                "/boom now", "u1"));
        Assert.Equal(RequestErrorKind.Blocked, err.Kind);
        Assert.Contains("unknown command", err.Message);
        Assert.Empty(transport.Sent);
        client.Dispose();
    }

    // ----- connect / disconnect paths ---------------------------------------

    [Fact]
    public async Task ConnectAsync_ReplacesStaleTransport_AndUnwiresItsEvents()
    {
        var first = new ScriptedTransport();
        var second = new ScriptedTransport();
        var transports = new Queue<ScriptedTransport>(new[] { first, second });
        var client = new ChirpClient("ws://test", _ => transports.Dequeue(),
            new ChirpClientOptions { RequestTimeoutMs = 10_000 });
        await client.ConnectAsync();
        await client.ConnectAsync(); // explicit reconnect: first is now stale

        // The stale transport's close must not look like our link dying.
        first.SimulateRemoteClose();
        Assert.Equal(ConnStatus.Connected, client.Status);
        Assert.True(first.Disposed);

        // The new transport carries traffic.
        var resp = await EchoAsync(client, second, new SendOptions { ReceiverId = "p" }, "hi");
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        client.Dispose();
    }

    [Fact]
    public async Task ConnectAsync_OpenFailure_SchedulesReconnect_SecondAttemptSucceeds()
    {
        var failing = new ScriptedTransport { OpenError = new InvalidOperationException("dial") };
        var good = new ScriptedTransport();
        var transports = new Queue<ScriptedTransport>(new[] { failing, good });
        var reconnected = new TaskCompletionSource<object?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var reconnectings = new List<(int Attempt, int DelayMs)>();
        var client = new ChirpClient("ws://test", _ => transports.Dequeue(),
            new ChirpClientOptions
            {
                RequestTimeoutMs = 10_000,
                ReconnectBaseMs = 20,
                ReconnectMaxMs = 100,
                JitterRatio = 0,
            });
        client.Reconnecting += (attempt, delay) => reconnectings.Add((attempt, delay));
        client.Reconnected += () => reconnected.TrySetResult(null);

        var err = await Assert.ThrowsAsync<RequestError>(() => client.ConnectAsync());
        Assert.Equal(RequestErrorKind.Closed, err.Kind);
        Assert.Equal(ConnStatus.WaitingReconnect, client.Status);

        await reconnected.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(ConnStatus.Connected, client.Status);
        Assert.Single(reconnectings);
        Assert.True(failing.Disposed);
        client.Dispose();
    }

    [Fact]
    public async Task ConnectAsync_AlwaysFailingOpen_ReconnectAttemptsKeepTrying()
    {
        var made = 0;
        var reconnectings = 0;
        var client = new ChirpClient("ws://test", _ =>
        {
            made++;
            return new ScriptedTransport { OpenError = new InvalidOperationException("dial") };
        }, new ChirpClientOptions
        {
            RequestTimeoutMs = 10_000,
            ReconnectBaseMs = 10,
            ReconnectMaxMs = 30,
            JitterRatio = 0,
        });
        client.Reconnecting += (attempt, delay) => Interlocked.Increment(ref reconnectings);

        await Assert.ThrowsAsync<RequestError>(() => client.ConnectAsync());
        // Every scheduled attempt fails its open and schedules the next one.
        WaitUntil(() => Volatile.Read(ref reconnectings) >= 2, "a second reconnect attempt");
        client.Dispose();
        Assert.True(made >= 2);
    }

    [Fact]
    public async Task ConnectAsync_DisposedDuringOpen_ReturnsSilently()
    {
        var gated = new ScriptedTransport
        {
            OpenGate = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously),
        };
        var client = new ChirpClient("ws://test", _ => gated,
            new ChirpClientOptions { RequestTimeoutMs = 10_000 });

        var opening = client.ConnectAsync();
        await Settle();
        client.Dispose(); // mid-open: the open still completes afterwards
        gated.OpenGate.TrySetResult(true);

        await opening.WaitAsync(TimeSpan.FromSeconds(5)); // no exception
        Assert.True(gated.Disposed);
    }

    [Fact]
    public async Task Disconnect_WithLiveTransport_ClosesExactlyOnce()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);

        client.Disconnect();
        await Settle();
        Assert.Equal(ConnStatus.Closed, client.Status);
        Assert.Equal(1, transport.CloseCount);

        client.Disconnect(); // idempotent: no second close
        Assert.Equal(1, transport.CloseCount);

        client.Dispose();
        await Assert.ThrowsAsync<ObjectDisposedException>(() => client.ConnectAsync());
    }

    [Fact]
    public void RequestAndSend_BeforeConnect_ThrowClosed()
    {
        var client = new ChirpClient("ws://test");
        var req = Assert.Throws<RequestError>(() =>
            client.RequestAsync(Specs.Login, new Chirp.Auth.LoginRequest { Token = "t" })
                .GetAwaiter().GetResult());
        Assert.Equal(RequestErrorKind.Closed, req.Kind);
        var send = Assert.Throws<RequestError>(
            () => client.Send(MsgID.TypingIndicatorNotify, new byte[] { 1 }));
        Assert.Equal(RequestErrorKind.Closed, send.Kind);
        client.Dispose();
    }

    [Fact]
    public async Task TransportSendFailure_RejectsPendingRequestAsClosed()
    {
        var transport = new ScriptedTransport { ThrowOnSend = true };
        var client = await ConnectAsync(transport);

        var err = await Assert.ThrowsAsync<RequestError>(() =>
            client.RequestAsync(Specs.Login, new Chirp.Auth.LoginRequest { Token = "t" }));
        Assert.Equal(RequestErrorKind.Closed, err.Kind);
        client.Dispose();
    }

    [Fact]
    public async Task Heartbeat_MissedPongs_KillTheLink()
    {
        var made = 0;
        var first = new ScriptedTransport();
        var client = new ChirpClient("ws://test", _ =>
        {
            made++;
            // Only the first dial succeeds; every reconnect keeps failing so
            // the client parks in WaitingReconnect instead of racing past it.
            return made == 1
                ? first
                : new ScriptedTransport { OpenError = new InvalidOperationException("dial") };
        }, new ChirpClientOptions
        {
            RequestTimeoutMs = 10_000,
            HeartbeatIntervalMs = 15,
            MaxMissedPongs = 1,
            ReconnectBaseMs = 20,
            ReconnectMaxMs = 100,
            JitterRatio = 0,
        });
        await client.ConnectAsync();

        // Two unanswered pings: the second one must close the transport and
        // flip to WaitingReconnect instead of pinging into the void forever.
        WaitUntil(() => first.CloseCount >= 1, "the dead-link close");
        WaitUntil(() => client.Status == ConnStatus.WaitingReconnect,
            "WaitingReconnect after dead link");
        client.Dispose();
        Assert.True(made >= 1);
    }

    // ----- inbound frame edges ----------------------------------------------

    [Fact]
    public async Task FrameError_KillsTransport_AndSchedulesReconnect()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);

        // Announced frame length far beyond the cap: the decoder refuses,
        // the client tears the transport down.
        transport.ServerRaw(new byte[] { 0x7F, 0xFF, 0xFF, 0xFF });
        WaitUntil(() => transport.CloseCount >= 1, "transport close after frame error");
        WaitUntil(() => client.Status == ConnStatus.WaitingReconnect,
            "WaitingReconnect after frame error");
        client.Dispose();
    }

    [Fact]
    public async Task UndecodablePacket_IsIgnored_ConnectionStaysUsable()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);

        // Truncated varint: not a Packet. The frame is dropped, the link
        // lives on.
        transport.ServerRaw(FrameCodec.Encode(new byte[] { 0xC8, 0x01 }));
        Assert.Equal(ConnStatus.Connected, client.Status);

        var resp = await EchoAsync(client, transport, new SendOptions { ReceiverId = "p" }, "hi");
        Assert.Equal(Chirp.Common.ErrorCode.Ok, resp.Code);
        client.Dispose();
    }

    [Fact]
    public async Task MalformedPong_ProvesLiveness_ButSetsNoOffset()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.HeartbeatPong,
            Sequence = 4242,
            Body = ByteString.CopyFrom(new byte[] { 0xFF, 0xFF }),
        });
        Assert.Null(client.ClockOffsetMs); // parse failed: no offset learned
        Assert.Equal(ConnStatus.Connected, client.Status);
        client.Dispose();
    }

    [Fact]
    public async Task KickNotify_GarbageBody_KicksWithEmptyReason_AndStaysKicked()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        client.AddListener(listener);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.KickNotify,
            Body = ByteString.CopyFrom(new byte[] { 0xFF }),
        });
        Assert.Equal(ConnStatus.Kicked, client.Status);
        Assert.Equal(new[] { "" }, listener.KickReasons.ToArray());

        // The transport close the kick triggers must land in the kicked arm,
        // not in a reconnect schedule.
        WaitUntil(() => transport.CloseCount >= 1, "kick close");
        await Settle();
        Assert.Equal(ConnStatus.Kicked, client.Status);
        Assert.Empty(listener.Reconnectings);
        client.Dispose();
    }

    [Fact]
    public async Task ChatNotify_GarbageBody_FallsBackToRawDispatch()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        client.SetMessageStore(new MemoryMessageStore()); // activates pipeline
        var rawBodies = new List<byte[]>();
        client.OnNotify(MsgID.ChatMessageNotify, rawBodies.Add);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(new byte[] { 0xFF }),
        });
        await Settle();
        // Parse failed: nothing stored, but the raw body still dispatched.
        Assert.Single(rawBodies);
        Assert.Empty(client.LoadHistory(Chirp.Chat.ChannelType.World, "*", 10));
        client.Dispose();
    }

    [Fact]
    public async Task RawNotify_ThrowingHandler_DoesNotStarveOthers()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var seen = 0;
        client.OnNotify(MsgID.TypingIndicatorNotify, _ => throw new InvalidOperationException());
        client.OnNotify(MsgID.TypingIndicatorNotify, _ => seen++);

        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.TypingIndicatorNotify,
            Sequence = 0,
            Body = ByteString.CopyFrom(new byte[] { 1 }),
        });
        await Settle();
        Assert.Equal(1, seen);
        client.Dispose();
    }

    [Fact]
    public async Task ReceivePipeline_HookFaults_AreContained()
    {
        var transport = new ScriptedTransport();
        var client = await ConnectAsync(transport);
        var listener = new RecordingListener();
        client.AddListener(listener);
        var rawBodies = new List<byte[]>();
        client.OnNotify(MsgID.ChatMessageNotify, rawBodies.Add);

        // A: throwing OnBeforeReceive drops the message everywhere.
        client.SetMessageInterceptor(new ThrowingBeforeReceiveInterceptor());
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(Msg("m1", "w", 1).ToByteArray()),
        });
        await Settle();
        Assert.Empty(listener.Messages);
        Assert.Empty(rawBodies);

        // B: throwing store.Save still delivers.
        client.SetMessageInterceptor(null!);
        client.SetMessageStore(new ThrowingStore());
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(Msg("m2", "w", 2).ToByteArray()),
        });
        await Settle();
        Assert.Equal(new[] { "m2" }, listener.Messages.Select(m => m.MessageId).ToArray());
        Assert.Single(rawBodies);

        // C: throwing OnAfterReceive still delivers too.
        client.SetMessageInterceptor(new ThrowingAfterReceiveInterceptor());
        client.SetMessageStore(null!);
        transport.ServerPacket(new Packet
        {
            MsgId = MsgID.ChatMessageNotify,
            Body = ByteString.CopyFrom(Msg("m3", "w", 3).ToByteArray()),
        });
        await Settle();
        Assert.Equal(new[] { "m2", "m3" }, listener.Messages.Select(m => m.MessageId).ToArray());
        Assert.Equal(2, rawBodies.Count);
        client.Dispose();
    }

    // ----- hook interface defaults (sdks/core contract) ----------------------

    [Fact]
    public async Task HookDefaults_MatchTheCppContract()
    {
        // Interceptor defaults: allow sends/receives, no-op after-hooks.
        IMessageInterceptor interceptor = new BareInterceptor();
        Assert.True(interceptor.OnBeforeSend(new Chirp.Chat.SendMessageRequest()));
        interceptor.OnAfterSend(new Chirp.Chat.SendMessageRequest());
        Assert.True(interceptor.OnBeforeReceive(new Chirp.Chat.ChatMessage()));
        interceptor.OnAfterReceive(new Chirp.Chat.ChatMessage());

        // Auth provider defaults: renewal declines, result hook is a no-op.
        IAuthProvider provider = new TokenOnlyProvider("t");
        Assert.Null(await provider.RenewTokenAsync());
        provider.OnAuthResult(Chirp.Common.ErrorCode.Ok, "u1");

        // Store defaults: no read tracking.
        IMessageStore store = new SaveLoadOnlyStore();
        store.MarkRead(Chirp.Chat.ChannelType.World, "w", "m1");
        Assert.Equal(0, store.GetUnreadCount(Chirp.Chat.ChannelType.World, "w"));
        store.Cleanup(0);

        // Listener defaults: every callback optional.
        IChatEventListener listener = new BareListener();
        listener.OnConnectionStateChanged(ConnStatus.Connected);
        listener.OnLoginResult(Chirp.Common.ErrorCode.Ok, "u1");
        listener.OnKicked("reason");
        listener.OnReconnecting(1, 10);
        listener.OnReconnected();
        listener.OnMessageReceived(new Chirp.Chat.ChatMessage());
        listener.OnUnreadChanged(Chirp.Chat.ChannelType.World, "w", 0);
        listener.OnPresenceChanged("u1", true);
        listener.OnTypingIndicator(new Chirp.Chat.TypingIndicator());
        listener.OnMarqueeMessage("text");
        listener.OnSystemAnnouncement("text");

        // Command defaults: usage line is "/name".
        ICommandHandler command = new RollCommand();
        Assert.Equal("/roll", command.Usage);
    }

    // ----- error copy + codec/spec small arms -------------------------------

    [Fact]
    public void ErrorText_CoversEveryMappedCode_PlusFallback()
    {
        Assert.Equal("成功", ChirpErrorText.Of(Chirp.Common.ErrorCode.Ok));
        Assert.Equal("服务器内部错误", ChirpErrorText.Of(Chirp.Common.ErrorCode.InternalError));
        Assert.Equal("参数错误", ChirpErrorText.Of(Chirp.Common.ErrorCode.InvalidParam));
        Assert.Equal("认证失败", ChirpErrorText.Of(Chirp.Common.ErrorCode.AuthFailed));
        Assert.Equal("登录已过期,请重新登录", ChirpErrorText.Of(Chirp.Common.ErrorCode.SessionExpired));
        Assert.Equal("用户不存在", ChirpErrorText.Of(Chirp.Common.ErrorCode.UserNotFound));
        Assert.Equal("对方离线,消息将在其上线后送达",
            ChirpErrorText.Of(Chirp.Common.ErrorCode.TargetOffline));
        Assert.Equal("服务暂不可用,请稍后再试",
            ChirpErrorText.Of(Chirp.Common.ErrorCode.ServerUnavailable));
        Assert.Equal("操作过于频繁,请稍后再试",
            ChirpErrorText.Of(Chirp.Common.ErrorCode.RateLimited));
        // Unmapped codes render their numeric value instead of throwing.
        Assert.Contains("未知错误", ChirpErrorText.Of((Chirp.Common.ErrorCode)9999));

        // Default messages per RequestErrorKind (no explicit message given).
        Assert.Equal("消息未发送", new RequestError(RequestErrorKind.Blocked).Message);
        Assert.Equal("请求超时", new RequestError(RequestErrorKind.Timeout).Message);
        Assert.Equal("连接已断开", new RequestError(RequestErrorKind.Closed).Message);
        Assert.Equal("已在其他设备登录", new RequestError(RequestErrorKind.Kicked).Message);
        Assert.Equal("认证失败", new RequestError(RequestErrorKind.Server,
            Chirp.Common.ErrorCode.AuthFailed).Message);
    }

    [Fact]
    public void CodecAndSpec_SmallArms()
    {
        // Encode refuses payloads beyond the defensive cap.
        var huge = new byte[FrameCodec.MaxFrameBytes + 1];
        Assert.Throws<FrameError>(() => FrameCodec.Encode(huge));

        // The decoder reports held bytes while a frame is still split.
        var decoder = new FrameDecoder();
        var frame = FrameCodec.Encode(new byte[] { 1, 2, 3 });
        Assert.Empty(decoder.Feed(frame[..3]));
        Assert.Equal(3, decoder.Buffered);
        Assert.Single(decoder.Feed(frame[3..]));
        Assert.Equal(0, decoder.Buffered);

        // The spec exposes both wire ids.
        Assert.Equal(MsgID.LoginReq, Specs.Login.ReqMsgId);
        Assert.Equal(MsgID.LoginResp, Specs.Login.RespMsgId);
    }

    // ----- FileMessageStore edges -------------------------------------------

    private static string TempPath()
    {
        var dir = Path.Combine(Path.GetTempPath(), "chirp-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, "archive.log");
    }

    private static void WriteRecord(FileStream stream, byte kind, byte[] payload)
    {
        stream.WriteByte(kind);
        var len = BitConverter.GetBytes((uint)payload.Length);
        stream.Write(len, 0, 4);
        stream.Write(payload, 0, payload.Length);
    }

    [Fact]
    public void FileStore_ReplayCorruptRecord_KeepsReplayablePrefix()
    {
        var path = TempPath();
        using (var fs = new FileStream(path, FileMode.Create))
        {
            var magic = Encoding.ASCII.GetBytes("CHIRPLOG1");
            fs.Write(magic, 0, magic.Length);
            WriteRecord(fs, 0x01, Msg("good", "w", 1).ToByteArray());
            WriteRecord(fs, 0x01, new byte[] { 0xFF, 0xFF, 0xFF, 0xFF }); // undecodable
        }
        var store = new FileMessageStore(path);
        Assert.Equal("good",
            store.Load(Chirp.Chat.ChannelType.World, "w", 10).Single().MessageId);
    }

    [Fact]
    public void FileStore_Compact_DropsDeadReadMarks()
    {
        var path = TempPath();
        var store = new FileMessageStore(path);
        // A read mark for a channel that never had messages is dead weight.
        store.MarkRead(Chirp.Chat.ChannelType.World, "ghost", "mX");
        store.Compact();

        var blob = File.ReadAllBytes(path);
        Assert.DoesNotContain("ghost", Encoding.Latin1.GetString(blob));
    }

    // ----- test doubles ------------------------------------------------------

    private sealed class ScriptedTransport : IChirpTransport
    {
        public event Action<byte[]>? BinaryMessage;
        public event Action? Closed;

        public Exception? OpenError;
        public TaskCompletionSource<bool>? OpenGate;
        public bool ThrowOnSend;
        public int CloseCount;
        public bool Disposed;
        public readonly List<byte[]> Sent = new();
        private readonly object _gate = new();

        public async Task OpenAsync(string url, CancellationToken ct)
        {
            if (OpenGate != null)
            {
                await OpenGate.Task.ConfigureAwait(false);
            }
            if (OpenError != null)
            {
                throw OpenError;
            }
        }

        public Task SendAsync(byte[] data)
        {
            if (ThrowOnSend)
            {
                throw new RequestError(RequestErrorKind.Closed);
            }
            lock (_gate) Sent.Add(data);
            return Task.CompletedTask;
        }

        public Task CloseAsync()
        {
            Interlocked.Increment(ref CloseCount);
            Closed?.Invoke();
            return Task.CompletedTask;
        }

        public void Dispose() => Disposed = true;

        public void ServerRaw(byte[] bytes) => BinaryMessage?.Invoke(bytes);

        public void ServerPacket(Packet packet) =>
            ServerRaw(FrameCodec.Encode(packet.ToByteArray()));

        public void SimulateRemoteClose() => Closed?.Invoke();

        public Packet LastSentPacket()
        {
            byte[] last;
            lock (_gate) last = Sent[^1];
            var decoder = new FrameDecoder();
            return Packet.Parser.ParseFrom(decoder.Feed(last).Single());
        }
    }

    private sealed class RecordingListener : IChatEventListener
    {
        public readonly List<Chirp.Common.ErrorCode> LoginCodes = new();
        public readonly List<string> KickReasons = new();
        public readonly List<(int Attempt, int DelayMs)> Reconnectings = new();
        public readonly List<Chirp.Chat.ChatMessage> Messages = new();

        public void OnLoginResult(Chirp.Common.ErrorCode code, string userId) =>
            LoginCodes.Add(code);
        public void OnKicked(string reason) => KickReasons.Add(reason);
        public void OnReconnecting(int attempt, int delayMs) =>
            Reconnectings.Add((attempt, delayMs));
        public void OnMessageReceived(Chirp.Chat.ChatMessage message) => Messages.Add(message);
    }

    private sealed class RecordingStatusListener : IChatEventListener
    {
        private readonly List<ConnStatus> _seen;
        public RecordingStatusListener(List<ConnStatus> seen) => _seen = seen;
        public void OnConnectionStateChanged(ConnStatus status) => _seen.Add(status);
    }

    private sealed class ThrowingEventListener : IChatEventListener
    {
        public void OnConnectionStateChanged(ConnStatus status) =>
            throw new InvalidOperationException("bad listener");
    }

    private sealed class FaultyProvider : IAuthProvider
    {
        public int AuthResults;

        public string GetToken() => "t1";

        public Task<string?> RenewTokenAsync() =>
            throw new InvalidOperationException("provider down");

        public void OnAuthResult(Chirp.Common.ErrorCode code, string userId)
        {
            AuthResults++;
            throw new InvalidOperationException("bad auth-result hook");
        }
    }

    private sealed class ThrowingBeforeSendInterceptor : IMessageInterceptor
    {
        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request) =>
            throw new InvalidOperationException("bad before-send hook");
    }

    private sealed class ThrowingAfterSendInterceptor : IMessageInterceptor
    {
        public bool OnAfterSendCalled;

        public bool OnBeforeSend(Chirp.Chat.SendMessageRequest request) => true;

        public void OnAfterSend(Chirp.Chat.SendMessageRequest request)
        {
            OnAfterSendCalled = true;
            throw new InvalidOperationException("bad after-send hook");
        }
    }

    private sealed class ThrowingBeforeReceiveInterceptor : IMessageInterceptor
    {
        public bool OnBeforeReceive(Chirp.Chat.ChatMessage message) =>
            throw new InvalidOperationException("bad before-receive hook");
    }

    private sealed class ThrowingAfterReceiveInterceptor : IMessageInterceptor
    {
        public bool OnBeforeReceive(Chirp.Chat.ChatMessage message) => true;

        public void OnAfterReceive(Chirp.Chat.ChatMessage message) =>
            throw new InvalidOperationException("bad after-receive hook");
    }

    private sealed class ThrowingStore : IMessageStore
    {
        public void Save(Chirp.Chat.ChatMessage message) =>
            throw new IOException("disk full");

        public List<Chirp.Chat.ChatMessage> Load(Chirp.Chat.ChannelType type,
            string channelId, int limit, long beforeTimestamp = 0) => new();
    }

    private sealed class BareInterceptor : IMessageInterceptor
    {
    }

    private sealed class TokenOnlyProvider : IAuthProvider
    {
        private readonly string _token;
        public TokenOnlyProvider(string token) => _token = token;
        public string GetToken() => _token;
    }

    private sealed class SaveLoadOnlyStore : IMessageStore
    {
        public void Save(Chirp.Chat.ChatMessage message) { }

        public List<Chirp.Chat.ChatMessage> Load(Chirp.Chat.ChannelType type,
            string channelId, int limit, long beforeTimestamp = 0) => new();
    }

    private sealed class BareListener : IChatEventListener
    {
    }

    private sealed class RollCommand : ICommandHandler
    {
        public string Name => "roll";
        public string Description => "roll a die";
        public bool Execute(string args, string senderId) => true;
    }

    private sealed class ThrowingCommand : ICommandHandler
    {
        public string Name => "boom";
        public string Description => "always fails";
        public bool Execute(string args, string senderId) =>
            throw new InvalidOperationException("bad command");
    }
}
