import ChirpProtos
import ChirpProtocol
import XCTest
@testable import ChirpAppCore

/// P2 最小闭环的服务级向量:假传输上跑通 登录→收消息(回执)→发送→
/// 断线入队→重连重放。驱动手法与 ChirpProtocolTests 同款(同步 open、
/// 手工 deliver、ManualScheduler 控重连节拍)。
final class ChatSessionServiceTests: XCTestCase {
    private var events: [ChatServiceEvent] = []
    private var scheduler: ManualScheduler!

    override func setUp() {
        super.setUp()
        events = []
        scheduler = ManualScheduler()
    }

    private func makeService(
        factory: @escaping (String) -> WsTransport,
        sessionSnapshot: SessionIndex.SnapshotIO? = nil
    ) -> ChatSessionService {
        ChatSessionService(
            userId: "alice",
            deviceId: "ios-test",
            chatUrl: "ws://127.0.0.1:7001",
            transportFactory: factory,
            scheduler: scheduler,
            random: FakeRandom(),
            // now 固定 1000 —— 回执 receivedAt 与发送侧存档时间戳可断言。
            now: { 1_000 },
            sessionSnapshot: sessionSnapshot
        ) { [weak self] in self?.events.append($0) }
    }

    /// 单传输工厂(无重连场景);测试直接持有 t 读帧/注帧。
    private func singleTransport() -> (FakeWsTransport, (String) -> WsTransport) {
        let t = FakeWsTransport()
        return (t, { _ in t })
    }

    /// 登录到 OK:请求帧带 token/deviceId/platform,终态事件 loginSucceeded。
    private func loginOk(_ service: ChatSessionService, _ transport: FakeWsTransport) throws {
        let pending = service.login()
        let seq = try expectRequest(transport, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        transport.deliver(try wsResponse(.loginResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertTrue(events.contains { if case .loginSucceeded = $0 { return true }; return false })
    }

    func testLoginRoundTripOk() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        let pending = service.login()
        let seq = try expectRequest(t, .loginReq)
        // 请求载荷:dev 阶段用户名即 token;platform 上报 ios。
        let request = try Chirp_Auth_LoginRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.token, "alice")
        XCTAssertEqual(request.deviceID, "ios-test")
        XCTAssertEqual(request.platform, "ios")

        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.loginResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(service.connectionState, .connected)
        service.logout()
    }

    func testLoginRejectedSurfacesEvent() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        let pending = service.login()
        let seq = try expectRequest(t, .loginReq)
        var rejected = Chirp_Auth_LoginResponse()
        rejected.code = .authFailed
        t.deliver(try wsResponse(.loginResp, seq, rejected))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .authFailed)
        XCTAssertTrue(events.contains {
            if case .loginRejected(let code) = $0 { return code == .authFailed }
            return false
        })
        service.logout()
    }

    func testIncomingNotifySendsAckBeforeIndexing() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let before = t.sent.count
        t.deliver(try wsNotify(.chatMessageNotify, dmText("bob", "alice", id: "m1", text: "在吗", ts: 900)))

        // 回执:MESSAGE_ACK,echo messageId,user=自己,receivedAt=注入时钟。
        let ackPacket = try sentPacket(t)
        XCTAssertEqual(ackPacket.msgID, .messageAck)
        XCTAssertTrue(t.sent.count > before)
        let ack = try Chirp_Chat_MessageAck(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(ack.messageID, "m1")
        XCTAssertEqual(ack.userID, "alice")
        XCTAssertEqual(ack.receivedAt, 1_000)

        // 会话索引与事件(入站 DM 折排序对键)。
        XCTAssertEqual(service.sessions.summary(key: "p:alice|bob")?.unread, 1)
        XCTAssertTrue(events.contains {
            if case .messageReceived(let m) = $0 { return m.messageID == "m1" }
            return false
        })
        service.logout()
    }

    func testSendDmRoundTripAndArchive() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let pending = service.send(peerId: "bob", content: "你好")
        let seq = try expectRequest(t, .sendMessageReq)
        let request = try Chirp_Chat_SendMessageRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.receiverID, "bob")
        XCTAssertEqual(request.channelID, "alice|bob", "DM 频道=排序对拼接")

        var ok = Chirp_Chat_SendMessageResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.sendMessageResp, seq, ok))
        guard case .sent = try pending.get(timeoutSeconds: 2) else {
            return XCTFail("expected .sent")
        }
        // 会话预览(发出侧不涨未读)+ 存档可查(store 侧存档副本 messageID 空)。
        let summary = service.sessions.summary(key: "p:alice|bob")
        XCTAssertEqual(summary?.lastMessage, "你好")
        XCTAssertEqual(summary?.unread, 0)
        let history = service.history(key: "p:alice|bob")
        XCTAssertEqual(history.count, 1)
        XCTAssertEqual(history.first?.messageID, "")
        XCTAssertEqual(history.first?.channelID, "alice|bob")
        service.logout()
    }

    func testSendWhileOfflineQueuesAndReplaysAfterReconnect() throws {
        let first = FakeWsTransport()
        var second: FakeWsTransport?
        var factoryCalls = 0
        let service = makeService { _ in
            factoryCalls += 1
            if factoryCalls == 1 { return first }  // 首连
            if second == nil { second = FakeWsTransport() }  // 重连新传输
            return second!
        }
        try loginOk(service, first)

        // 断线(不走本地 close 挥手):待发请求被冲刷为 CLOSED。
        first.dropLink()
        scheduler.advance(0)

        let pending = service.send(peerId: "bob", content: "离线消息")
        guard case .queuedOffline = try pending.get(timeoutSeconds: 2) else {
            return XCTFail("expected .queuedOffline")
        }
        XCTAssertTrue(events.contains {
            if case .offlineQueued(let content) = $0 { return content == "离线消息" }
            return false
        })

        // 退避首跳(base 500ms,jitter=0)后自动重连;新传输上重放队列。
        scheduler.advance(600)
        guard let t2 = second else { return XCTFail("no reconnect transport") }
        let seq = try expectRequest(t2, .sendMessageReq)
        var ok = Chirp_Chat_SendMessageResponse()
        ok.code = .ok
        t2.deliver(try wsResponse(.sendMessageResp, seq, ok))
        XCTAssertTrue(events.contains {
            if case .offlineReplayed(let count) = $0 { return count == 1 }
            return false
        })
        XCTAssertEqual(service.sessions.summary(key: "p:alice|bob")?.lastMessage, "离线消息")
        service.logout()
    }

    func testKickNotifiesAndBlocksAutoReconnect() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        var kick = Chirp_Auth_KickNotify()
        kick.reason = "elsewhere"
        t.deliver(try wsNotify(.kickNotify, kick))
        XCTAssertTrue(events.contains {
            if case .kicked(let reason) = $0 { return reason == "elsewhere" }
            return false
        })
        XCTAssertEqual(service.connectionState, .kicked)
        service.logout()
    }

    // ---- P4a:反应/输入状态/服务端历史 ----------------------------------------

    func testReactionNotifyUpdatesIndexAndEmitsEvent() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        var added = Chirp_Chat_ReactionAddedNotify()
        added.messageID = "m1"
        added.channelID = "alice|bob"
        added.emoji = "👍"
        added.userID = "bob"
        added.timestamp = 1_000
        t.deliver(try wsNotify(.reactionAddedNotify, added))

        XCTAssertEqual(service.reactions.tallies(messageId: "m1").count, 1)
        XCTAssertEqual(service.reactions.tallies(messageId: "m1")[0].count, 1)
        XCTAssertFalse(service.reactions.isMine(messageId: "m1", emoji: "👍"))
        XCTAssertTrue(events.contains {
            if case .reactionChanged(let messageId) = $0 { return messageId == "m1" }
            return false
        })

        // 移除走同一数据面。
        var removed = Chirp_Chat_ReactionRemovedNotify()
        removed.messageID = "m1"
        removed.channelID = "alice|bob"
        removed.emoji = "👍"
        removed.userID = "bob"
        removed.timestamp = 1_100
        t.deliver(try wsNotify(.reactionRemovedNotify, removed))
        XCTAssertTrue(service.reactions.tallies(messageId: "m1").isEmpty)
        service.logout()
    }

    func testLoginDevicesFromLoginResponseAppliedAndEmitted() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)

        // 登录响应带初始清单(服务端已排除本会话)→ 入索引 + 事件。
        let pending = service.login()
        let seq = try expectRequest(t, .loginReq)
        var ok = Chirp_Auth_LoginResponse()
        ok.code = .ok
        var web = Chirp_Auth_DevicePresence()
        web.platform = "web"
        web.deviceID = "web-1"
        web.online = true
        ok.onlineDevices = [web]
        t.deliver(try wsResponse(.loginResp, seq, ok))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)

        XCTAssertEqual(service.devices.entries().count, 1)
        XCTAssertEqual(service.devices.entries()[0].deviceId, "web-1")
        XCTAssertTrue(events.contains { if case .devicesChanged = $0 { return true }; return false })
        service.logout()
    }

    func testDevicesPresenceNotifyUpdatesIndexAndLogoutResets() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        var notify = Chirp_Auth_DevicesPresenceNotify()
        var web = Chirp_Auth_DevicePresence()
        web.platform = "web"
        web.deviceID = "web-2"
        web.online = true
        var android = Chirp_Auth_DevicePresence()
        android.platform = "android"
        android.deviceID = "and-1"
        android.online = true
        notify.devices = [web, android]
        t.deliver(try wsNotify(.devicesPresenceNotify, notify))
        XCTAssertEqual(
            service.devices.entries().map(\.deviceId), ["and-1", "web-2"],
            "platform 字典序")

        // offline 条目保留(last-seen)。
        var offline = Chirp_Auth_DevicePresence()
        offline.platform = "android"
        offline.deviceID = "and-1"
        offline.online = false
        var gone = Chirp_Auth_DevicesPresenceNotify()
        gone.devices = [offline]
        t.deliver(try wsNotify(.devicesPresenceNotify, gone))
        XCTAssertEqual(service.devices.entries().count, 2)
        XCTAssertFalse(
            service.devices.entries().first { $0.platform == "android" }!.online)

        XCTAssertEqual(
            events.filter { if case .devicesChanged = $0 { return true }; return false }.count, 2)
        // logout 丢弃旧账号镜像。
        service.logout()
        XCTAssertTrue(service.devices.entries().isEmpty)
    }

    func testAddReactionSendsThreeFieldsAndAppliesAggregate() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let pending = service.addReaction(messageId: "m1", emoji: "🎉")
        let seq = try expectRequest(t, .addReactionReq)
        let request = try Chirp_Chat_AddReactionRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.messageID, "m1")
        XCTAssertEqual(request.userID, "alice", "请求 userID=登录身份(SameUser 守卫)")
        XCTAssertEqual(request.emoji, "🎉")

        // 服务端聚合覆盖本地槽(操作者不在 notify 扇出面,应答即真相)。
        var response = Chirp_Chat_AddReactionResponse()
        response.code = .ok
        response.serverTime = 1_200
        response.reaction.messageID = "m1"
        response.reaction.emoji = "🎉"
        response.reaction.count = 3
        response.reaction.userIds = ["alice", "bob", "carol"]
        response.reaction.reactedByMe = true
        t.deliver(try wsResponse(.addReactionResp, seq, response))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)

        let tallies = service.reactions.tallies(messageId: "m1")
        XCTAssertEqual(tallies.count, 1)
        XCTAssertEqual(tallies[0].count, 3)
        XCTAssertTrue(tallies[0].isMine)
        XCTAssertTrue(events.contains {
            if case .reactionChanged(let messageId) = $0 { return messageId == "m1" }
            return false
        })
        service.logout()
    }

    func testRemoveReactionDecrementsLocallyOnOk() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        // 他人先到一条计数,自己再撤——RESP 无聚合,本地递减(web 同款)。
        var other = Chirp_Chat_ReactionAddedNotify()
        other.messageID = "m1"
        other.channelID = "alice|bob"
        other.emoji = "👍"
        other.userID = "bob"
        other.timestamp = 1_000
        t.deliver(try wsNotify(.reactionAddedNotify, other))

        let pending = service.removeReaction(messageId: "m1", emoji: "👍")
        let seq = try expectRequest(t, .removeReactionReq)
        let request = try Chirp_Chat_RemoveReactionRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.messageID, "m1")
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.emoji, "👍")

        var response = Chirp_Chat_RemoveReactionResponse()
        response.code = .ok
        t.deliver(try wsResponse(.removeReactionResp, seq, response))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertTrue(service.reactions.tallies(messageId: "m1").isEmpty)
        service.logout()
    }

    func testTypingNotifyRecordsAndEmits() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        var indicator = Chirp_Chat_TypingIndicator()
        indicator.channelID = "alice|bob"
        indicator.channelType = .private
        indicator.userID = "bob"
        indicator.username = "bob"
        indicator.isTyping = true
        indicator.timestamp = 1_000
        t.deliver(try wsNotify(.typingIndicatorNotify, indicator))

        XCTAssertEqual(
            service.typists.typists(channelType: .private, channelId: "alice|bob", nowMs: 1_000),
            ["bob"])
        XCTAssertTrue(events.contains {
            if case .typing(let channelId, let userId, let isTyping) = $0 {
                return channelId == "alice|bob" && userId == "bob" && isTyping
            }
            return false
        })
        service.logout()
    }

    func testSendTypingSendsIndicatorBody() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let before = t.sent.count
        service.sendTyping(channelType: .private, channelId: "alice|bob", isTyping: true)
        XCTAssertEqual(t.sent.count, before + 1, "fire-and-forget 直发,无应答")
        XCTAssertEqual(try sentPacket(t).msgID, .typingIndicatorNotify)
        let body = try Chirp_Chat_TypingIndicator(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(body.channelID, "alice|bob")
        XCTAssertEqual(body.channelType, .private)
        XCTAssertEqual(body.userID, "alice")
        XCTAssertEqual(body.username, "alice", "展示名=userId(web 同款)")
        XCTAssertTrue(body.isTyping)
        XCTAssertEqual(body.timestamp, 1_000, "now 固定 1000")
        service.logout()
    }

    func testLoadServerHistoryRoundTrip() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let pending = service.loadServerHistory(key: "p:alice|bob", limit: 50)
        let seq = try expectRequest(t, .getHistoryReq)
        let request = try Chirp_Chat_GetHistoryRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.channelType, .private)
        XCTAssertEqual(request.channelID, "alice|bob")
        XCTAssertEqual(request.limit, 50)
        XCTAssertEqual(request.beforeTimestamp, 0, "0=取最新一页")

        var response = Chirp_Chat_GetHistoryResponse()
        response.code = .ok
        response.messages = [
            dmText("bob", "alice", id: "m1", text: "早", ts: 900),
            dmText("alice", "bob", id: "m2", text: "早", ts: 901),
        ]
        response.hasMore_p = true
        t.deliver(try wsResponse(.getHistoryResp, seq, response))
        let resp = try pending.get(timeoutSeconds: 2)
        XCTAssertEqual(resp.code, .ok)
        XCTAssertEqual(resp.messages.map(\.messageID), ["m1", "m2"])
        XCTAssertTrue(resp.hasMore_p)
        service.logout()
    }

    // ---- P4e:群面 ------------------------------------------------------------

    /// 应答当前最后一条 GET_USER_GROUPS(loginOk 后的引导帧或显式重拉),
    /// 注入一份名单。
    private func deliverGroups(
        _ t: FakeWsTransport, _ groups: [Chirp_Chat_GroupInfo]
    ) throws {
        let seq = try expectRequest(t, .getUserGroupsReq)
        var response = Chirp_Chat_GetUserGroupsResponse()
        response.code = .ok
        response.groups = groups
        t.deliver(try wsResponse(.getUserGroupsResp, seq, response))
    }

    func testGroupIncomingAcksAndBucketsSession() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        t.deliver(try wsNotify(
            .chatMessageNotify, groupText("bob", groupId: "g1", id: "m1", text: "群内早", ts: 900)))

        // 群消息同样先回执。
        XCTAssertEqual(try sentPacket(t).msgID, .messageAck)
        // 索引折 'g:' 桶,不折发信人的 DM 行。
        XCTAssertEqual(service.sessions.summary(key: "g:g1")?.unread, 1)
        XCTAssertEqual(service.sessions.summary(key: "g:g1")?.kind, .group)
        XCTAssertNil(service.sessions.summary(key: "p:alice|bob"))
        XCTAssertTrue(events.contains {
            if case .messageReceived(let m) = $0 { return m.messageID == "m1" }
            return false
        })
        service.logout()
    }

    func testGroupSendWireShapeAndSessionPreview() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let pending = service.send(channel: .group("g1"), content: "群内早")
        let seq = try expectRequest(t, .sendMessageReq)
        let request = try Chirp_Chat_SendMessageRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.channelType, .guild)
        XCTAssertEqual(request.channelID, "g1")
        XCTAssertEqual(request.receiverID, "", "群发送 receiver 留空(chat_validation)")

        var ok = Chirp_Chat_SendMessageResponse()
        ok.code = .ok
        t.deliver(try wsResponse(.sendMessageResp, seq, ok))
        guard case .sent = try pending.get(timeoutSeconds: 2) else {
            return XCTFail("expected .sent")
        }
        let summary = service.sessions.summary(key: "g:g1")
        XCTAssertEqual(summary?.lastMessage, "群内早")
        XCTAssertEqual(summary?.unread, 0, "发出侧不涨未读")
        service.logout()
    }

    /// 登录引导帧消费名单:整表换入 + 每群一行空会话 + groupsChanged;
    /// 显式重拉的请求体带 0=不分页口径。
    func testRefreshGroupsRoundTripBootstrapsRows() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        try deliverGroups(t, [groupInfo("g2", name: "二号", owner: "bob"),
                              groupInfo("g1", name: "一号", owner: "alice")])

        XCTAssertEqual(service.groups.entries().map(\.groupId), ["g1", "g2"])
        XCTAssertEqual(service.groups.name(groupId: "g1"), "一号")
        let bootstrapped = service.sessions.summary(key: "g:g1")
        XCTAssertEqual(bootstrapped?.kind, .group)
        XCTAssertEqual(bootstrapped?.lastMessage, "", "空行:预览由消息记账填")
        XCTAssertTrue(events.contains { if case .groupsChanged = $0 { return true }; return false })

        // 显式重拉:请求体字段 + 整表换入生效(少一群即少一行)。
        let pending = service.refreshGroups()
        let seq = try expectRequest(t, .getUserGroupsReq)
        let request = try Chirp_Chat_GetUserGroupsRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.limit, 0, "0=不分页(web 同款)")
        XCTAssertEqual(request.offset, 0)
        var response = Chirp_Chat_GetUserGroupsResponse()
        response.code = .ok
        response.groups = [groupInfo("g1", name: "一号", owner: "alice")]
        t.deliver(try wsResponse(.getUserGroupsResp, seq, response))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(service.groups.entries().map(\.groupId), ["g1"])
        XCTAssertNil(service.sessions.summary(key: "g:g2"), "整表换入不保留旧群行")
        service.logout()
    }

    func testCreateGroupReturnsGroupIdAndChainsRefresh() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)
        // 引导帧先答掉,别占着最后一条。
        try deliverGroups(t, [])

        let pending = service.createGroup(name: "研发群")
        let seq = try expectRequest(t, .createGroupReq)
        let request = try Chirp_Chat_CreateGroupRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.creatorID, "alice")
        XCTAssertEqual(request.groupName, "研发群")

        var response = Chirp_Chat_CreateGroupResponse()
        response.code = .ok
        response.groupID = "g9"
        t.deliver(try wsResponse(.createGroupResp, seq, response))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), "g9")
        // 成功链一次名单重拉(web createGroup 同序)。
        XCTAssertEqual(try sentPacket(t).msgID, .getUserGroupsReq)

        // 空名短路:不发帧,直接回失败空串(去空白是壳层的活,服务面只挡空串)。
        let frames = t.sent.count
        XCTAssertEqual(try service.createGroup(name: "").get(timeoutSeconds: 2), "")
        XCTAssertEqual(t.sent.count, frames)
        service.logout()
    }

    func testInviteAndKickSendFieldShapesAndChainRefresh() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)
        try deliverGroups(t, [groupInfo("g1", name: "一号", owner: "alice")])

        let invite = service.inviteToGroup(groupId: "g1", targetUserId: "bob")
        let inviteSeq = try expectRequest(t, .inviteToGroupReq)
        let inviteBody = try Chirp_Chat_InviteToGroupRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(inviteBody.inviterID, "alice")
        XCTAssertEqual(inviteBody.groupID, "g1")
        XCTAssertEqual(inviteBody.targetUserID, "bob")
        var inviteResp = Chirp_Chat_InviteToGroupResponse()
        inviteResp.code = .ok
        t.deliver(try wsResponse(.inviteToGroupResp, inviteSeq, inviteResp))
        XCTAssertEqual(try invite.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(try sentPacket(t).msgID, .getUserGroupsReq, "成功刷名单")

        let kick = service.kickMember(groupId: "g1", targetUserId: "bob")
        let kickSeq = try expectRequest(t, .kickMemberReq)
        let kickBody = try Chirp_Chat_KickMemberRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(kickBody.requesterID, "alice")
        XCTAssertEqual(kickBody.groupID, "g1")
        XCTAssertEqual(kickBody.targetUserID, "bob")
        var kickResp = Chirp_Chat_KickMemberResponse()
        kickResp.code = .ok
        t.deliver(try wsResponse(.kickMemberResp, kickSeq, kickResp))
        XCTAssertEqual(try kick.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(try sentPacket(t).msgID, .getUserGroupsReq, "成功刷名单")
        service.logout()
    }

    /// 退群:服务端对 actor 不回 notify,成功即本地双删并发
    /// sessionDropped(.left)——不再发重拉帧(web leaveGroup 同款本地清)。
    func testLeaveGroupLocalDoubleDeleteWithoutRefetch() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)
        try deliverGroups(t, [groupInfo("g1", name: "一号", owner: "bob")])
        t.deliver(try wsNotify(
            .chatMessageNotify, groupText("bob", groupId: "g1", id: "m1", text: "早", ts: 900)))
        XCTAssertEqual(service.sessions.summary(key: "g:g1")?.unread, 1)

        let pending = service.leaveGroup(groupId: "g1")
        let seq = try expectRequest(t, .leaveGroupReq)
        let request = try Chirp_Chat_LeaveGroupRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.groupID, "g1")

        let framesBeforeResponse = t.sent.count
        var response = Chirp_Chat_LeaveGroupResponse()
        response.code = .ok
        t.deliver(try wsResponse(.leaveGroupResp, seq, response))
        XCTAssertEqual(try pending.get(timeoutSeconds: 2), .ok)
        XCTAssertEqual(t.sent.count, framesBeforeResponse, "本地清,不重拉名单")
        XCTAssertNil(service.groups.entry(groupId: "g1"))
        XCTAssertNil(service.sessions.summary(key: "g:g1"), "未读随行一并丢弃")
        XCTAssertTrue(events.contains {
            if case .sessionDropped(let key, let reason) = $0 {
                return key == "g:g1" && reason == .left
            }
            return false
        })
        XCTAssertTrue(events.contains { if case .groupsChanged = $0 { return true }; return false })
        service.logout()
    }

    /// 2120 被踢的是我:本地双删 + sessionDropped(.kicked),不重拉。
    func testKickedSelfNotifyDropsLocallyWithoutRefetch() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)
        try deliverGroups(t, [groupInfo("g1", name: "一号", owner: "bob")])

        var notify = Chirp_Chat_GroupMemberKickedNotify()
        notify.groupID = "g1"
        notify.userID = "alice"
        notify.kickedBy = "bob"
        notify.timestamp = 1_000
        let frames = t.sent.count
        t.deliver(try wsNotify(.groupMemberKickedNotify, notify))
        XCTAssertEqual(t.sent.count, frames, "自己被踢走本地清,不重拉")
        XCTAssertNil(service.groups.entry(groupId: "g1"))
        XCTAssertNil(service.sessions.summary(key: "g:g1"))
        XCTAssertTrue(events.contains {
            if case .sessionDropped(let key, let reason) = $0 {
                return key == "g:g1" && reason == .kicked
            }
            return false
        })
        service.logout()
    }

    /// 别人被踢 / JOINED / LEFT / UPDATED:整表重拉一条 GET_USER_GROUPS。
    func testKickedOtherAndRosterNotifiesTriggerRefresh() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)
        try deliverGroups(t, [])

        var other = Chirp_Chat_GroupMemberKickedNotify()
        other.groupID = "g1"
        other.userID = "carol"
        other.kickedBy = "alice"
        t.deliver(try wsNotify(.groupMemberKickedNotify, other))
        XCTAssertEqual(try sentPacket(t).msgID, .getUserGroupsReq)

        // 这三路不看 body(扇出重拉),空 body 也不伤链路。
        for msgId in [Chirp_Gateway_MsgID.groupMemberJoinedNotify,
                      .groupMemberLeftNotify,
                      .groupUpdatedNotify] {
            t.deliver(try wsNotify(msgId, Chirp_Chat_GroupMemberKickedNotify()))
            XCTAssertEqual(try sentPacket(t).msgID, .getUserGroupsReq)
        }
        service.logout()
    }

    func testLoadGroupMembersRoundTrip() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let pending = service.loadGroupMembers(groupId: "g1")
        let seq = try expectRequest(t, .getGroupMembersReq)
        let request = try Chirp_Chat_GetGroupMembersRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.groupID, "g1")
        XCTAssertEqual(request.limit, 0, "0=不分页(web 同款)")
        XCTAssertEqual(request.offset, 0)

        var owner = Chirp_Chat_GroupMember()
        owner.userID = "alice"
        owner.username = "alice"
        owner.role = .moderator
        var member = Chirp_Chat_GroupMember()
        member.userID = "bob"
        member.username = "bob"
        var response = Chirp_Chat_GetGroupMembersResponse()
        response.code = .ok
        response.members = [owner, member]
        response.totalCount = 2
        t.deliver(try wsResponse(.getGroupMembersResp, seq, response))

        let resp = try pending.get(timeoutSeconds: 2)
        XCTAssertEqual(resp.code, .ok)
        XCTAssertEqual(resp.members.map(\.userID), ["alice", "bob"])
        XCTAssertEqual(resp.members[0].role, .moderator)
        XCTAssertEqual(resp.totalCount, 2)
        service.logout()
    }

    /// 群消息断线入队,重连重放成功后落 'g:' 会话行(离线队列闭包对群生效)。
    func testGroupOfflineReplayRecordsGroupSession() throws {
        let first = FakeWsTransport()
        var second: FakeWsTransport?
        var factoryCalls = 0
        let service = makeService { _ in
            factoryCalls += 1
            if factoryCalls == 1 { return first }
            if second == nil { second = FakeWsTransport() }
            return second!
        }
        try loginOk(service, first)

        first.dropLink()
        scheduler.advance(0)

        let pending = service.send(channel: .group("g1"), content: "离线群消息")
        guard case .queuedOffline = try pending.get(timeoutSeconds: 2) else {
            return XCTFail("expected .queuedOffline")
        }

        scheduler.advance(600)
        guard let t2 = second else { return XCTFail("no reconnect transport") }
        let seq = try expectRequest(t2, .sendMessageReq)
        let request = try Chirp_Chat_SendMessageRequest(serializedBytes: lastRequestBody(t2))
        XCTAssertEqual(request.channelType, .guild, "重放保真:仍是群帧")
        XCTAssertEqual(request.channelID, "g1")
        var ok = Chirp_Chat_SendMessageResponse()
        ok.code = .ok
        t2.deliver(try wsResponse(.sendMessageResp, seq, ok))
        XCTAssertTrue(events.contains {
            if case .offlineReplayed(let count) = $0 { return count == 1 }
            return false
        })
        XCTAssertEqual(service.sessions.summary(key: "g:g1")?.lastMessage, "离线群消息")
        service.logout()
    }

    func testLoadServerHistoryGuildPaginationShape() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        // beforeTimestamp=翻页游标(上一页最早一条的时间戳)。
        let pending = service.loadServerHistory(key: "g:g1", limit: 30, beforeTimestamp: 900)
        let seq = try expectRequest(t, .getHistoryReq)
        let request = try Chirp_Chat_GetHistoryRequest(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(request.userID, "alice")
        XCTAssertEqual(request.channelType, .guild, "'g:' 键折 GUILD 频道")
        XCTAssertEqual(request.channelID, "g1")
        XCTAssertEqual(request.limit, 30)
        XCTAssertEqual(request.beforeTimestamp, 900)

        var response = Chirp_Chat_GetHistoryResponse()
        response.code = .ok
        response.messages = [groupText("bob", groupId: "g1", id: "m0", text: "昨天", ts: 800)]
        response.hasMore_p = false
        t.deliver(try wsResponse(.getHistoryResp, seq, response))
        let resp = try pending.get(timeoutSeconds: 2)
        XCTAssertEqual(resp.messages.map(\.messageID), ["m0"])
        XCTAssertFalse(resp.hasMore_p)
        service.logout()
    }

    func testSendTypingGuildVariant() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        service.sendTyping(channelType: .guild, channelId: "g1", isTyping: true)
        XCTAssertEqual(try sentPacket(t).msgID, .typingIndicatorNotify)
        let body = try Chirp_Chat_TypingIndicator(serializedBytes: lastRequestBody(t))
        XCTAssertEqual(body.channelID, "g1")
        XCTAssertEqual(body.channelType, .guild)
        XCTAssertTrue(body.isTyping)
        service.logout()
    }

    /// 坏键(登出竞态/结构破损):历史查询返回空,服务端拉取直接失败,不猜频道。
    func testBadSessionKeyFailsClosed() throws {
        let (t, factory) = singleTransport()
        let service = makeService(factory: factory)
        try loginOk(service, t)

        let frames = t.sent.count
        XCTAssertEqual(service.history(key: "garbage"), [])
        service.markRead(key: "garbage")  // no-op,不发帧
        XCTAssertThrowsError(try service.loadServerHistory(key: "garbage").get(timeoutSeconds: 2))
        XCTAssertEqual(t.sent.count, frames, "坏键在本地挡下,不上线")
        service.logout()
    }

    /// P6 接线:入站消息真实路径记账→落盘;换实例(重启语义)构造即回灌,
    /// 预览与未读跨实例存活。
    func testSessionSnapshotPersistsAcrossServiceInstances() throws {
        let mem = MemorySnapshotIO()
        let (t1, f1) = singleTransport()
        let first = makeService(factory: f1, sessionSnapshot: mem.io())
        try loginOk(first, t1)
        t1.deliver(try wsNotify(
            .chatMessageNotify, dmText("bob", "alice", id: "m1", text: "在吗", ts: 900)))
        XCTAssertEqual(mem.saves, 1, "入站记账即落盘(登录面不动会话索引)")

        let second = makeService(
            factory: { _ in FakeWsTransport() }, sessionSnapshot: mem.io())
        let summary = second.sessions.summary(key: "p:alice|bob")
        XCTAssertEqual(summary?.lastMessage, "在吗")
        XCTAssertEqual(summary?.unread, 1, "未读跨实例存活")
        first.logout()
    }
}
