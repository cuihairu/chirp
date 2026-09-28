package chirp.mobile.protocol

import chirp.app_notification.AppNotification
import chirp.auth.Auth
import chirp.chat.Chat
import chirp.game_server_gateway.GameServerGateway
import chirp.gateway.Gateway
import chirp.party.Party
import chirp.social.Social
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class MsgSpecsTest {
    @Test
    fun msgIdConstantsMatchTheProtoContract() {
        // Pinned against proto/gateway.proto so a renumber cannot slip through
        // this client unnoticed.
        assertEquals(1001, Gateway.MsgID.HEARTBEAT_PING_VALUE)
        assertEquals(1002, Gateway.MsgID.HEARTBEAT_PONG_VALUE)
        assertEquals(1003, Gateway.MsgID.LOGIN_REQ_VALUE)
        assertEquals(1004, Gateway.MsgID.LOGIN_RESP_VALUE)
        assertEquals(1005, Gateway.MsgID.KICK_NOTIFY_VALUE)
        assertEquals(1020, Gateway.MsgID.DEVICES_PRESENCE_NOTIFY_VALUE)
        assertEquals(2005, Gateway.MsgID.CHAT_MESSAGE_NOTIFY_VALUE)
    }

    @Test
    fun specTableIsCompleteAndPairsStayDistinct() {
        assertEquals(40, MsgSpecs.all.size)
        for (spec in MsgSpecs.all) {
            assertTrue(
                spec.reqMsgId != spec.respMsgId,
                "req/resp ids must differ for ${spec.reqMsgId}",
            )
        }
    }

    @Test
    fun everySpecDecodesAnEmptyPayloadIntoItsResponseType() {
        // proto3: an empty body parses to the default instance; the assertion
        // pins that each decoder is wired to the matching generated class.
        assertTrue(MsgSpecs.login.decodeResponse(ByteArray(0)) is Auth.LoginResponse)
        assertTrue(MsgSpecs.sendMessage.decodeResponse(ByteArray(0)) is Chat.SendMessageResponse)
        assertTrue(MsgSpecs.getHistory.decodeResponse(ByteArray(0)) is Chat.GetHistoryResponse)
        assertTrue(MsgSpecs.addFriend.decodeResponse(ByteArray(0)) is Social.AddFriendResponse)
        assertTrue(MsgSpecs.createParty.decodeResponse(ByteArray(0)) is Party.CreatePartyResponse)
        assertTrue(MsgSpecs.registerDevice.decodeResponse(ByteArray(0)) is AppNotification.RegisterDeviceResponse)
        assertTrue(
            MsgSpecs.setGamePresenceEnabled.decodeResponse(ByteArray(0)) is
                GameServerGateway.SetGamePresenceEnabledResponse,
        )
        for (spec in MsgSpecs.all) {
            spec.decodeResponse(ByteArray(0)) // must not throw for any spec
        }
    }
}
