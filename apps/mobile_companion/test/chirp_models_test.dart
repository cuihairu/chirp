import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';

void main() {
  group('ChirpMessage', () {
    test('parses a full JSON payload', () {
      final message = ChirpMessage.fromJson(const {
        'message_id': 'm1',
        'sender_id': 'user_1',
        'receiver_id': 'user_2',
        'channel_id': 'chan_1',
        'channel_type': 1,
        'msg_type': 2,
        'content': 'hello',
        'timestamp': 1705489000000,
      });

      expect(message.messageId, 'm1');
      expect(message.senderId, 'user_1');
      expect(message.receiverId, 'user_2');
      expect(message.channelId, 'chan_1');
      expect(message.channelType, ChannelType.team);
      expect(message.msgType, MsgType.voice);
      expect(message.content, 'hello');
      expect(message.timestamp, 1705489000000);
    });

    test('applies defaults for missing fields', () {
      final message = ChirpMessage.fromJson(const {});

      expect(message.messageId, '');
      expect(message.senderId, '');
      expect(message.receiverId, '');
      expect(message.channelId, '');
      expect(message.channelType, ChannelType.private);
      expect(message.msgType, MsgType.text);
      expect(message.content, '');
      expect(message.timestamp, 0);
    });

    test('round-trips through toJson', () {
      const payload = {
        'message_id': 'm1',
        'sender_id': 'user_1',
        'receiver_id': 'user_2',
        'channel_id': 'chan_1',
        'channel_type': 3,
        'msg_type': 4,
        'content': 'hello',
        'timestamp': 1705489000000,
      };

      final json = ChirpMessage.fromJson(payload).toJson();

      expect(json, payload);
    });
  });

  group('Voice events', () {
    test('VoiceIceCandidateEvent parses all fields', () {
      final event = VoiceIceCandidateEvent.fromJson(const {
        'from_user_id': 'user_1',
        'candidate': 'candidate:1 1 UDP',
        'sdp_mid': 'audio',
        'sdp_mline_index': 0,
      });

      expect(event.fromUserId, 'user_1');
      expect(event.candidate, 'candidate:1 1 UDP');
      expect(event.sdpMid, 'audio');
      expect(event.sdpMLineIndex, 0);
    });

    test('VoiceSdpOfferEvent parses all fields', () {
      final event = VoiceSdpOfferEvent.fromJson(const {
        'from_user_id': 'user_1',
        'sdp_offer': 'v=0...',
      });

      expect(event.fromUserId, 'user_1');
      expect(event.sdpOffer, 'v=0...');
    });

    test('VoiceSpeakingEvent parses all fields', () {
      final event = VoiceSpeakingEvent.fromJson(const {
        'user_id': 'user_1',
        'speaking': true,
      });

      expect(event.userId, 'user_1');
      expect(event.isSpeaking, isTrue);
    });

    test('VoiceSpeakingEvent defaults speaking to false', () {
      final event = VoiceSpeakingEvent.fromJson(const {});

      expect(event.isSpeaking, isFalse);
    });
  });

  group('VoiceParticipant', () {
    test('defaults speaking and muted to false', () {
      final participant = VoiceParticipant(userId: 'u1', username: 'Alice');

      expect(participant.isSpeaking, isFalse);
      expect(participant.isMuted, isFalse);
    });
  });

  group('Channel and message types', () {
    test('channel type indices match the wire format', () {
      expect(
        ChannelType.values.map((t) => t.index).toList(),
        [0, 1, 2, 3],
      );
    });

    test('message type indices match the wire format', () {
      expect(
        MsgType.values.map((t) => t.index).toList(),
        [0, 1, 2, 3, 4],
      );
    });

    test('voice room type indices match the wire format', () {
      expect(
        VoiceRoomType.values.map((t) => t.index).toList(),
        [0, 1, 2],
      );
    });
  });
}
