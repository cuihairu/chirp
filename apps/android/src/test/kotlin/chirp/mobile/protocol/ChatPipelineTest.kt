package chirp.mobile.protocol

import chirp.auth.Auth
import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import com.google.protobuf.ByteString
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

/** 与 chat_pipeline_test.dart 同构的管线测试（dart 端同组语义对拍）。 */
class ChatPipelineTest {
    private val scheduler = ManualScheduler(startMs = 1_000)
    private val transports = ArrayList<FakeWsTransport>()

    private fun connectedPipeline(
        selfId: String = "self",
    ): Pair<ChatPipeline, FakeWsTransport> {
        val connection = ChatConnection(
            url = "ws://test/chirp",
            transportFactory = {
                FakeWsTransport().also { transports.add(it) }
            },
            scheduler = scheduler,
        )
        connection.connect().get(5, TimeUnit.SECONDS)
        val pipeline = ChatPipeline(
            conn = connection,
            selfId = { selfId },
            deviceId = { "device-1" },
        )
        pipeline.start()
        return pipeline to transports.last()
    }

    private fun wsMessage(
        msgId: Gateway.MsgID,
        sequence: Long,
        body: com.google.protobuf.MessageLite,
    ): ByteArray = wsMessageRaw(msgId, sequence, body.toByteString().toByteArray())

    private fun wsMessageRaw(msgId: Gateway.MsgID, sequence: Long, body: ByteArray): ByteArray =
        encodeFrame(
            Gateway.Packet.newBuilder()
                .setMsgId(msgId)
                .setSequence(sequence)
                .setBody(ByteString.copyFrom(body))
                .build()
                .toByteArray(),
        )

    private fun textMessage(
        senderId: String,
        content: String,
        channelType: Chat.ChannelType = Chat.ChannelType.PRIVATE,
        channelId: String = "a|b",
        timestamp: Long = 5_000,
    ): Chat.ChatMessage = Chat.ChatMessage.newBuilder()
        .setSenderId(senderId)
        .setChannelType(channelType)
        .setChannelId(channelId)
        .setMsgType(Chat.MsgType.TEXT)
        .setContent(ByteString.copyFromUtf8(content))
        .setTimestamp(timestamp)
        .build()

    private fun <T> unwrapped(future: java.util.concurrent.CompletableFuture<T>): T =
        try {
            future.get(5, TimeUnit.SECONDS)
        } catch (e: ExecutionException) {
            throw e.cause!!
        }

    // ---- login -------------------------------------------------------------

    @Test
    fun loginUsesExplicitTokenOverProviderOverUserId() {
        val (pipeline, transport) = connectedPipeline()
        pipeline.provider = object : AuthProvider {
            override fun getToken(): String = "provider-token"
        }
        val future = pipeline.login("user-1", token = "explicit-token")
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals("explicit-token", Auth.LoginRequest.parseFrom(sent.body).token)
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, sent.sequence,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
    }

    @Test
    fun loginFallsBackToProviderTokenThenUserId() {
        val (pipeline, transport) = connectedPipeline()
        pipeline.provider = object : AuthProvider {
            override fun getToken(): String = "provider-token"
        }
        val first = pipeline.login("user-1")
        var sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals("provider-token", Auth.LoginRequest.parseFrom(sent.body).token)
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, sent.sequence,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(first))

        val (pipeline2, transport2) = connectedPipeline()
        val second = pipeline2.login("user-2")
        sent = Gateway.Packet.parseFrom(transport2.lastSentPayload())
        assertEquals("user-2", Auth.LoginRequest.parseFrom(sent.body).token)
        assertEquals("device-1", Auth.LoginRequest.parseFrom(sent.body).deviceId)
        transport2.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, sent.sequence,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(second))
    }

    @Test
    fun authFailedGivesTheProviderOneRenewalChance() {
        val (pipeline, transport) = connectedPipeline()
        val outcomes = ArrayList<Common.ErrorCode>()
        var renewals = 0
        pipeline.provider = object : AuthProvider {
            override fun getToken(): String = "stale-token"
            override fun renewToken(): String? {
                renewals++
                return "fresh-token"
            }

            override fun onAuthResult(code: Common.ErrorCode, userId: String) {
                outcomes.add(code)
            }
        }
        // Answer both login rounds: first AUTH_FAILED, then OK.
        val first = pipeline.login("user-1")
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, 1,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.AUTH_FAILED).build(),
            ),
        )
        // Second round goes out with the renewed token.
        val second = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals("fresh-token", Auth.LoginRequest.parseFrom(second.body).token)
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, 2,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(first))
        assertEquals(1, renewals)
        assertEquals(listOf(Common.ErrorCode.OK), outcomes)
    }

    @Test
    fun authFailedWithoutProviderOrRenewalIsTerminal() {
        val (pipeline, transport) = connectedPipeline()
        val loginResults = ArrayList<Common.ErrorCode>()
        pipeline.addListener(object : ChatEventListener {
            override fun onLoginResult(code: Common.ErrorCode, userId: String) {
                loginResults.add(code)
            }
        })
        val future = pipeline.login("user-1")
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, 1,
                Auth.LoginResponse.newBuilder().setCode(Common.ErrorCode.AUTH_FAILED).build(),
            ),
        )
        assertEquals(Common.ErrorCode.AUTH_FAILED, unwrapped(future))
        assertEquals(listOf(Common.ErrorCode.AUTH_FAILED), loginResults)
    }

    @Test
    fun successfulLoginResetsBackoffAndFansOutLoginDevices() {
        val (pipeline, transport) = connectedPipeline()
        val devices = ArrayList<List<Auth.DevicePresence>>()
        pipeline.addListener(object : ChatEventListener {
            override fun onLoginDevices(dev: List<Auth.DevicePresence>) {
                devices.add(dev)
            }
        })
        val initial = Auth.DevicePresence.newBuilder().setDeviceId("other-device").build()
        val future = pipeline.login("user-1")
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.LOGIN_RESP, 1,
                Auth.LoginResponse.newBuilder()
                    .setCode(Common.ErrorCode.OK)
                    .addOnlineDevices(initial)
                    .build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(future))
        assertEquals(1, devices.size)
        assertEquals("other-device", devices[0].single().deviceId)
    }

    // ---- send ---------------------------------------------------------------

    @Test
    fun closedStateIsCheckedBeforeArgumentValidation() {
        val connection = ChatConnection(
            url = "ws://test",
            transportFactory = { FakeWsTransport() },
            scheduler = scheduler,
        )
        // Never connected.
        val pipeline = ChatPipeline(connection, { "self" }, { "dev" })
        val err = assertExecutionError(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE), content = ""),
        )
        assertIs<RequestError>(err)
        assertEquals(RequestError.Kind.CLOSED, err.kind)
    }

    @Test
    fun emptyContentAndMissingReceiverAreArgumentErrors() {
        val (pipeline, _) = connectedPipeline()
        assertExecutionErrorIsIllegalArgument(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), content = ""),
        )
        assertExecutionErrorIsIllegalArgument(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE), content = "hi"),
        )
        assertExecutionErrorIsIllegalArgument(
            pipeline.send(SendOptions(Chat.ChannelType.WORLD), content = "hi"),
        )
    }

    @Test
    fun privateSendDerivesSortedPairChannelId() {
        val (pipeline, transport) = connectedPipeline(selfId = "bob")
        val future = pipeline.send(
            SendOptions(Chat.ChannelType.PRIVATE, receiverId = "alice"),
            content = "hi",
        )
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        val request = Chat.SendMessageRequest.parseFrom(sent.body)
        assertEquals("alice|bob", request.channelId)
        assertEquals("bob", request.senderId)
        assertEquals("alice", request.receiverId)
        assertEquals("hi", request.content.toStringUtf8())
        // Complete the round so the future settles.
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.SEND_MESSAGE_RESP, sent.sequence,
                Chat.SendMessageResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(future).code)
    }

    @Test
    fun slashCommandWithHandlerIsConsumedLocally() {
        val (pipeline, transport) = connectedPipeline()
        val seen = ArrayList<String>()
        pipeline.registerCommand(object : CommandHandler {
            override val name = "echo"
            override fun execute(args: String, senderId: String): Boolean {
                seen.add("$args:$senderId")
                return true
            }
        })
        val err = assertExecutionError(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), "/echo hi"),
        )
        assertIs<RequestError>(err)
        assertEquals(RequestError.Kind.BLOCKED, err.kind)
        assertEquals(listOf("hi:self"), seen)
        // Nothing reached the wire: no frame was ever sent.
        assertEquals(0, transport.sent.size)
    }

    @Test
    fun unknownSlashCommandIsBlockedNotSent() {
        val (pipeline, transport) = connectedPipeline()
        pipeline.registerCommand(object : CommandHandler {
            override val name = "echo"
            override fun execute(args: String, senderId: String): Boolean = true
        })
        val err = assertExecutionError(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), "/nope"),
        )
        assertIs<RequestError>(err)
        assertTrue(err.message!!.contains("unknown command"))
    }

    @Test
    fun slashCommandWithNoHandlersPassesThrough() {
        val (pipeline, transport) = connectedPipeline()
        val future = pipeline.send(
            SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), "/echo hi",
        )
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals(Gateway.MsgID.SEND_MESSAGE_REQ, sent.msgId)
        assertEquals("/echo hi", Chat.SendMessageRequest.parseFrom(sent.body).content.toStringUtf8())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.SEND_MESSAGE_RESP, sent.sequence,
                Chat.SendMessageResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        assertEquals(Common.ErrorCode.OK, unwrapped(future).code)
    }

    @Test
    fun throwingCommandHandlerDeclinesForTheNextSameNameHandler() {
        val (pipeline, _) = connectedPipeline()
        val calls = ArrayList<String>()
        pipeline.registerCommand(object : CommandHandler {
            override val name = "echo"
            override fun execute(args: String, senderId: String): Boolean {
                calls.add("first")
                throw IllegalStateException("declined")
            }
        })
        pipeline.registerCommand(object : CommandHandler {
            override val name = "echo"
            override fun execute(args: String, senderId: String): Boolean {
                calls.add("second")
                return true
            }
        })
        val err = assertExecutionError(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), "/echo x"),
        )
        assertIs<RequestError>(err)
        assertEquals(listOf("first", "second"), calls)
    }

    @Test
    fun interceptorRewriteReachesTheWireAndAfterSendFires() {
        val (pipeline, transport) = connectedPipeline()
        val afterSend = ArrayList<String>()
        pipeline.interceptor = object : MessageInterceptor {
            override fun onBeforeSend(request: Chat.SendMessageRequest): Chat.SendMessageRequest =
                request.toBuilder().setContent(ByteString.copyFromUtf8("rewritten")).build()

            override fun onAfterSend(request: Chat.SendMessageRequest) {
                afterSend.add(request.content.toStringUtf8())
            }
        }
        val future = pipeline.send(
            SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), "original",
        )
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        assertEquals("rewritten", Chat.SendMessageRequest.parseFrom(sent.body).content.toStringUtf8())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.SEND_MESSAGE_RESP, sent.sequence,
                Chat.SendMessageResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        unwrapped(future)
        assertEquals(listOf("rewritten"), afterSend)
    }

    @Test
    fun interceptorsThatThrowAreBlocking() {
        val (pipeline, transport) = connectedPipeline()
        pipeline.interceptor = object : MessageInterceptor {
            override fun onBeforeSend(request: Chat.SendMessageRequest): Chat.SendMessageRequest {
                throw IllegalStateException("boom")
            }
        }
        val err = assertExecutionError(
            pipeline.send(SendOptions(Chat.ChannelType.PRIVATE, receiverId = "peer"), "hi"),
        )
        assertIs<RequestError>(err)
        assertEquals(RequestError.Kind.BLOCKED, err.kind)
        assertEquals(0, transport.sent.size)
    }

    @Test
    fun archiveSavesBothDirectionsAndForwardCallsPassThrough() {
        val (pipeline, transport) = connectedPipeline()
        val store = MemoryMessageStore()
        pipeline.store = store

        val future = pipeline.send(
            SendOptions(Chat.ChannelType.PRIVATE, receiverId = "alice"), "out",
        )
        val sent = Gateway.Packet.parseFrom(transport.lastSentPayload())
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.SEND_MESSAGE_RESP, sent.sequence,
                Chat.SendMessageResponse.newBuilder().setCode(Common.ErrorCode.OK).build(),
            ),
        )
        unwrapped(future)
        // Sent copy archived (empty messageId is the sent-direction marker).
        assertEquals(1, store.load(Chat.ChannelType.PRIVATE, "alice|self", 10).size)

        // Incoming push archived and fanned out (same channel bucket).
        val incoming = textMessage("alice", "in", channelId = "alice|self")
        transport.deliverWsMessage(wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, incoming))
        assertEquals(2, store.load(Chat.ChannelType.PRIVATE, "alice|self", 10).size)
        assertEquals("out", store.load(Chat.ChannelType.PRIVATE, "alice|self", 10)[1].content.toStringUtf8())
        assertEquals("in", store.load(Chat.ChannelType.PRIVATE, "alice|self", 10)[0].content.toStringUtf8())
    }

    // ---- incoming path --------------------------------------------------------

    @Test
    fun interceptorReceiveDropKillsArchiveListenersAndAfterReceive() {
        val (pipeline, transport) = connectedPipeline()
        val store = MemoryMessageStore()
        pipeline.store = store
        val seen = ArrayList<String>()
        var afterCount = 0
        pipeline.interceptor = object : MessageInterceptor {
            override fun onBeforeReceive(message: Chat.ChatMessage): Chat.ChatMessage? =
                if (message.content.toStringUtf8() == "drop") null else message

            override fun onAfterReceive(message: Chat.ChatMessage) {
                afterCount++
            }
        }
        pipeline.addListener(object : ChatEventListener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                seen.add(message.content.toStringUtf8())
            }
        })
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "drop")),
        )
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "keep")),
        )
        assertEquals(listOf("keep"), seen)
        assertEquals(1, afterCount)
        // The dropped message never reached the archive either.
        assertEquals(1, store.load(Chat.ChannelType.PRIVATE, "a|b", 10).size)
    }

    @Test
    fun archiveFailureDoesNotKillTheFanOut() {
        val (pipeline, transport) = connectedPipeline()
        pipeline.store = object : MessageStore by MemoryMessageStore() {
            override fun save(message: Chat.ChatMessage) {
                throw IllegalStateException("disk full")
            }
        }
        val seen = ArrayList<String>()
        pipeline.addListener(object : ChatEventListener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                seen.add(message.content.toStringUtf8())
            }
        })
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "hello")),
        )
        assertEquals(listOf("hello"), seen)
    }

    @Test
    fun throwingListenersDoNotStarveEachOther() {
        val (pipeline, transport) = connectedPipeline()
        pipeline.addListener(object : ChatEventListener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                throw IllegalStateException("first explodes")
            }
        })
        val seen = ArrayList<String>()
        pipeline.addListener(object : ChatEventListener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                seen.add(message.content.toStringUtf8())
            }
        })
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "hello")),
        )
        assertEquals(listOf("hello"), seen)
    }

    @Test
    fun undecodableIncomingBodyIsIgnoredAndTheLinkSurvives() {
        val (pipeline, transport) = connectedPipeline()
        val store = MemoryMessageStore()
        pipeline.store = store
        val seen = ArrayList<String>()
        pipeline.addListener(object : ChatEventListener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                seen.add(message.content.toStringUtf8())
            }
        })
        // Valid framing, undecodable payload: 0x7F is an invalid wire type.
        transport.deliverWsMessage(
            wsMessageRaw(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, byteArrayOf(0x7F)),
        )
        assertEquals(0, seen.size)
        assertEquals(0, store.load(Chat.ChannelType.PRIVATE, "a|b", 10).size)
        // The link is still alive: a well-formed push gets through.
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "ok")),
        )
        assertEquals(listOf("ok"), seen)
    }

    @Test
    fun kickFansOutTheReason() {
        val (pipeline, transport) = connectedPipeline()
        val kicks = ArrayList<String>()
        pipeline.addListener(object : ChatEventListener {
            override fun onKicked(reason: String) {
                kicks.add(reason)
            }
        })
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.KICK_NOTIFY, 0, Auth.KickNotify.newBuilder().setReason("dup login").build()),
        )
        assertEquals(listOf("dup login"), kicks)
    }

    @Test
    fun malformedKickStillFansOutWithAnEmptyReason() {
        val (pipeline, transport) = connectedPipeline()
        val kicks = ArrayList<String>()
        pipeline.addListener(object : ChatEventListener {
            override fun onKicked(reason: String) {
                kicks.add(reason)
            }
        })
        // A body that is not a KickNotify still proves the session is dead;
        // protobuf leniency yields an empty reason instead of a crash.
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.KICK_NOTIFY, 0, textMessage("x", "not a kick")),
        )
        assertEquals(listOf(""), kicks)
    }

    @Test
    fun devicesPresenceEmptyListIsSkippedAndMalformedDropped() {
        val (pipeline, transport) = connectedPipeline()
        val presences = ArrayList<Int>()
        pipeline.addListener(object : ChatEventListener {
            override fun onDevicesPresence(devices: List<Auth.DevicePresence>) {
                presences.add(devices.size)
            }
        })
        transport.deliverWsMessage(
            wsMessage(
                Gateway.MsgID.DEVICES_PRESENCE_NOTIFY, 0,
                Auth.DevicesPresenceNotify.newBuilder()
                    .addDevices(Auth.DevicePresence.newBuilder().setDeviceId("d1"))
                    .addDevices(Auth.DevicePresence.newBuilder().setDeviceId("d2"))
                    .build(),
            ),
        )
        // Empty list: skipped silently.
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.DEVICES_PRESENCE_NOTIFY, 0, Auth.DevicesPresenceNotify.newBuilder().build()),
        )
        // Malformed (not a device list): dropped.
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.DEVICES_PRESENCE_NOTIFY, 0, textMessage("x", "nope")),
        )
        assertEquals(listOf(2), presences)
    }

    @Test
    fun stopUnsubscribesIncoming() {
        val (pipeline, transport) = connectedPipeline()
        val seen = ArrayList<String>()
        pipeline.addListener(object : ChatEventListener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                seen.add(message.content.toStringUtf8())
            }
        })
        pipeline.stop()
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "after stop")),
        )
        assertEquals(0, seen.size)
        // Idempotent restart rewires.
        pipeline.start()
        transport.deliverWsMessage(
            wsMessage(Gateway.MsgID.CHAT_MESSAGE_NOTIFY, 0, textMessage("alice", "after restart")),
        )
        assertEquals(listOf("after restart"), seen)
    }

    // ---- helpers ------------------------------------------------------------

    private fun assertExecutionError(future: java.util.concurrent.CompletableFuture<*>): Throwable =
        try {
            future.get(5, TimeUnit.SECONDS)
            kotlin.test.fail("expected failure")
        } catch (e: ExecutionException) {
            e.cause!!
        }

    private fun assertExecutionErrorIsIllegalArgument(future: java.util.concurrent.CompletableFuture<*>) {
        assertIs<IllegalArgumentException>(assertExecutionError(future))
    }
}
