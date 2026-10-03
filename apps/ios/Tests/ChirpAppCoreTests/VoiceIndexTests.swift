import ChirpProtos
import XCTest
@testable import ChirpAppCore

/// 语音房间镜像(P4d):三方向拼 roster——整表拉取、无操作者 notify、
/// 自己 mute/deafen 本地回显。
final class VoiceIndexTests: XCTestCase {
    private func room(_ id: String, members: [(String, Chirp_Voice_ParticipantState)])
        -> VoiceIndex.RoomSnapshot
    {
        VoiceIndex.RoomSnapshot(
            roomId: id, roomName: "raid", maxParticipants: 8,
            participants: members.map {
                VoiceIndex.Participant(userId: $0.0, state: $0.1)
            })
    }

    func testApplyAndClearRoundTrip() {
        let index = VoiceIndex()
        XCTAssertNil(index.room())
        XCTAssertNil(index.roomId())
        index.apply(room("r1", members: [("alice", .connected)]))
        XCTAssertEqual(index.roomId(), "r1")
        XCTAssertEqual(index.room()?.participants.first?.userId, "alice")
        index.clear()
        XCTAssertNil(index.room())
    }

    func testUpsertReplacesSameUserAndIgnoresNoRoom() {
        let index = VoiceIndex()
        // 不在房:notify 一律忽略。
        index.upsert(.init(userId: "alice", state: .connected))
        XCTAssertNil(index.room())

        index.apply(room("r1", members: [("alice", .connected)]))
        index.upsert(.init(userId: "alice", state: .muted))
        index.upsert(.init(userId: "bob", state: .connected))
        XCTAssertEqual(
            index.room()?.participants.map(\.userId), ["alice", "bob"])
        XCTAssertEqual(
            index.room()?.participants.first { $0.userId == "alice" }?.state, .muted)
    }

    func testRemoveParticipantGuardsStaleRoomId() {
        let index = VoiceIndex()
        index.apply(room("r1", members: [("alice", .connected), ("bob", .connected)]))
        // 我们已换房 r2,晚到的 r1 left 不得误删。
        index.apply(room("r2", members: [("alice", .connected)]))
        index.removeParticipant(roomId: "r1", userId: "alice")
        XCTAssertEqual(index.roomId(), "r2")
        XCTAssertEqual(index.room()?.participants.map(\.userId), ["alice"])
        index.removeParticipant(roomId: "r2", userId: "alice")
        XCTAssertEqual(index.room()?.participants.map(\.userId), [])
    }

    func testUpdateStateKeepsFlagDetails() {
        let index = VoiceIndex()
        var snapshot = room("r1", members: [("alice", .connected)])
        snapshot.participants[0].muted = true
        index.apply(snapshot)
        index.updateState(roomId: "r1", userId: "alice", state: .deafened)
        let alice = index.room()?.participants.first
        XCTAssertEqual(alice?.state, .deafened)
        XCTAssertEqual(alice?.muted, true, "标志细节保留到下次整表拉取")
        index.updateState(roomId: "wrong", userId: "alice", state: .connected)
        XCTAssertEqual(index.room()?.participants.first?.state, .deafened)
    }

    func testApplySelfFlagsOnlyTouchesSelf() {
        let index = VoiceIndex()
        index.apply(room("r1", members: [("alice", .connected), ("bob", .connected)]))
        index.applySelfFlags(userId: "alice", muted: true)
        XCTAssertEqual(index.room()?.participants.first { $0.userId == "alice" }?.muted, true)
        XCTAssertNil(index.room()?.participants.first { $0.userId == "bob" }?.muted)
        index.applySelfFlags(userId: "alice", deafened: true)
        XCTAssertEqual(index.room()?.participants.first { $0.userId == "alice" }?.deafened, true)
    }
}
