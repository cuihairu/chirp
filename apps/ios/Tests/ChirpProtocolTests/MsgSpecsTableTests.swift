import XCTest
import ChirpProtos
@testable import ChirpProtocol

/// Same vector group as the Kotlin MsgSpecsTest.
final class MsgSpecsTableTests: XCTestCase {
    func testMsgIdConstantsMatchTheProtoContract() {
        // Pinned against proto/gateway.proto so a renumber cannot slip
        // through this client unnoticed.
        XCTAssertEqual(Chirp_Gateway_MsgID.heartbeatPing.rawValue, 1001)
        XCTAssertEqual(Chirp_Gateway_MsgID.heartbeatPong.rawValue, 1002)
        XCTAssertEqual(Chirp_Gateway_MsgID.loginReq.rawValue, 1003)
        XCTAssertEqual(Chirp_Gateway_MsgID.loginResp.rawValue, 1004)
        XCTAssertEqual(Chirp_Gateway_MsgID.kickNotify.rawValue, 1005)
        XCTAssertEqual(Chirp_Gateway_MsgID.devicesPresenceNotify.rawValue, 1020)
        XCTAssertEqual(Chirp_Gateway_MsgID.chatMessageNotify.rawValue, 2005)
    }

    func testSpecTableIsCompleteAndPairsStayDistinct() {
        XCTAssertEqual(MsgSpecs.all.count, 48)
        for spec in MsgSpecs.all {
            XCTAssertNotEqual(spec.reqMsgId, spec.respMsgId, "req/resp ids must differ for \(spec.reqMsgId)")
        }
    }

    func testEverySpecDecodesAnEmptyPayloadIntoItsResponseType() throws {
        // proto3: an empty body parses to the default instance; the assertion
        // pins that each decoder is wired to the matching generated class.
        XCTAssertTrue(try MsgSpecs.login.decodeResponse([]) is Chirp_Auth_LoginResponse)
        XCTAssertTrue(try MsgSpecs.sendMessage.decodeResponse([]) is Chirp_Chat_SendMessageResponse)
        XCTAssertTrue(try MsgSpecs.getHistory.decodeResponse([]) is Chirp_Chat_GetHistoryResponse)
        XCTAssertTrue(try MsgSpecs.addFriend.decodeResponse([]) is Chirp_Social_AddFriendResponse)
        XCTAssertTrue(try MsgSpecs.createParty.decodeResponse([]) is Chirp_Party_CreatePartyResponse)
        XCTAssertTrue(try MsgSpecs.registerDevice.decodeResponse([]) is Chirp_AppNotification_RegisterDeviceResponse)
        XCTAssertTrue(
            try MsgSpecs.setGamePresenceEnabled.decodeResponse([]) is Chirp_GameServerGateway_SetGamePresenceEnabledResponse)
        XCTAssertTrue(
            try MsgSpecs.wordFilterFetch.decodeResponse([]) is Chirp_Chat_WordFilterFetchResponse)
        for spec in MsgSpecs.all {
            try spec.decodeEmpty() // must not throw for any spec
        }
    }
}
