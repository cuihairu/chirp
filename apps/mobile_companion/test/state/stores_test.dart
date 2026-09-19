import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_mobile/state/conversation_store.dart';
import 'package:chirp_mobile/state/device_store.dart';
import 'package:chirp_mobile/state/friend_store.dart';
import 'package:chirp_mobile/state/message_store.dart';
import 'package:chirp_mobile/state/models.dart';
import 'package:chirp_mobile/state/party_store.dart';
import 'package:chirp_mobile/state/typing_presence.dart';

Conversation conversation(String key,
        {ConversationKind kind = ConversationKind.private,
        String title = 't',
        int unread = 0}) =>
    Conversation(
      kind: kind,
      key: key,
      channelId: key.substring(2),
      peerId: key.substring(2),
      title: title,
      unreadLocal: unread,
    );

void main() {
  group('conversation store', () {
    test('upsert merges without clobbering unread, sorted by activity', () {
      final store = createConversationStore();
      upsertConversation(store, conversation('p:b|a', title: 'A'));
      touchConversation(store, 'p:b|a', 'hi', 100);
      upsertConversation(store, conversation('p:d|c', title: 'B'));
      bumpUnread(store, 'p:b|a');

      // Re-upsert (e.g. roster refresh) must keep the unread count.
      upsertConversation(store, conversation('p:b|a', title: 'A2'));

      final list = store.value.conversations;
      expect(list, hasLength(2));
      // 'B' has no timestamp → sorted last despite insertion order.
      expect(list.first.key, 'p:b|a');
      expect(list.first.title, 'A2');
      expect(list.first.unreadLocal, 1);
      expect(list.first.lastMessagePreview, 'hi');
    });

    test('clearUnread and removeConversation', () {
      final store = createConversationStore();
      upsertConversation(
          store, conversation('g:g1', kind: ConversationKind.group));
      bumpUnread(store, 'g:g1');
      bumpUnread(store, 'g:g1');
      clearUnread(store, 'g:g1');
      expect(store.value.conversations.single.unreadLocal, 0);
      removeConversation(store, 'g:g1');
      expect(store.value.conversations, isEmpty);
    });
  });

  group('message store', () {
    ChatMessageView message(String id, String key, {String? clientId}) =>
        ChatMessageView(
          messageId: id,
          clientId: clientId,
          senderId: 'peer',
          channelKey: key,
          channelType: ChannelType.PRIVATE,
          channelId: 'a|b',
          content: 'text-$id',
          timestamp: 1,
          pending: false,
        );

    test('append dedupes by id and replaces pending by clientId', () {
      final store = createMessageStore();
      addPendingMessage(
          store, message('pending-x', 'k', clientId: 'pending-x'));
      // The echo of our own send must replace, not duplicate.
      appendMessage(store, message('m1', 'k', clientId: 'pending-x'));
      appendMessage(store, message('m1', 'k')); // duplicate dropped

      final list = messagesOf(store.value, 'k');
      expect(list, hasLength(1));
      expect(list.single.messageId, 'm1');
    });

    test('failPendingMessage flips the flag', () {
      final store = createMessageStore();
      addPendingMessage(store, message('p1', 'k', clientId: 'p1'));
      failPendingMessage(store, 'k', 'p1');
      expect(messagesOf(store.value, 'k').single.failed, isTrue);
      expect(messagesOf(store.value, 'k').single.pending, isFalse);
    });

    test('prependHistory dedupes and keeps order; hasMore tracked', () {
      final store = createMessageStore();
      appendMessage(store, message('m2', 'k'));
      prependHistory(store, 'k', [message('m1', 'k')], true);
      expect(
          messagesOf(store.value, 'k').map((m) => m.messageId), ['m1', 'm2']);
      prependHistory(store, 'k', [message('m1', 'k')], false); // overlap
      expect(messagesOf(store.value, 'k'), hasLength(2));
      expect(store.value.hasMore['k'], isFalse);
    });

    test('reaction apply/set/remove cycle', () {
      final store = createMessageStore();
      appendMessage(store, message('m1', 'k'));
      applyReaction(store, 'm1', '👍', true, true);
      expect(messagesOf(store.value, 'k').single.reactions['👍']!.count, 1);
      // The actor's own REMOVE notify also decrements locally.
      applyReaction(store, 'm1', '👍', true, false);
      expect(messagesOf(store.value, 'k').single.reactions, isEmpty);

      setReaction(store, 'm1',
          const MessageReactionView(emoji: '❤️', count: 3, mine: false));
      expect(messagesOf(store.value, 'k').single.reactions['❤️']!.count, 3);
    });

    test('read cursors, edit and delete by id', () {
      final store = createMessageStore();
      appendMessage(store, message('m1', 'k'));
      setReadCursor(store, 'k', 'peer', 'm1');
      expect(readCursorOf(store.value, 'k', 'peer'), 'm1');

      applyEditById(store, 'm1', 'new text');
      final edited = messagesOf(store.value, 'k').single;
      expect(edited.content, 'new text');
      expect(edited.edited, isTrue);

      applyDeleteById(store, 'm1');
      final deleted = messagesOf(store.value, 'k').single;
      expect(deleted.deleted, isTrue);
      expect(deleted.content, isEmpty);
    });

    test('clearChannel forgets everything about a channel', () {
      final store = createMessageStore();
      appendMessage(store, message('m1', 'k'));
      setHasMore(store, 'k', true);
      clearChannel(store, 'k');
      expect(messagesOf(store.value, 'k'), isEmpty);
    });
  });

  group('friend store', () {
    test('request lifecycle and roster ops', () {
      final store = createFriendStore();
      addPendingIn(store, 'r1', 'alice');
      addPendingIn(store, 'r1', 'alice'); // idempotent
      expect(store.value.pendingIn, hasLength(1));
      expect(fromUserIdOf(store.value, 'r1'), 'alice');

      resolvePending(store, 'r1');
      addFriend(store, 'alice');
      expect(store.value.friends, ['alice']);

      addPendingOut(store, 'alice'); // already a friend → ignored
      expect(store.value.pendingOut, isEmpty);
      addPendingOut(store, 'bob');
      expect(store.value.pendingOut, ['bob']);

      removeFriendStore(store, 'alice');
      expect(store.value.friends, isEmpty);
    });
  });

  group('party store', () {
    test('snapshot replaces, invites queue and drain, helpers', () {
      final store = createPartyStore();
      addInvite(store, 'i1', 'alice', 'party1');
      addInvite(store, 'i1', 'alice', 'party1'); // dedupe
      expect(store.value.invites, hasLength(1));

      applySnapshot(
          store,
          const PartySnapshot(
            partyId: 'party1',
            leaderId: 'alice',
            maxMembers: 5,
            members: [
              PartyMemberView(userId: 'alice', ready: true),
              PartyMemberView(userId: 'me', ready: false),
            ],
          ));
      expect(store.value.party!.members, hasLength(2));
      expect(isLeaderOf(store.value, 'alice'), isTrue);
      expect(isLeaderOf(store.value, 'me'), isFalse);
      expect(selfMemberOf(store.value, 'me')!.ready, isFalse);
      // Invites survive snapshot application (they are independent queues).
      expect(store.value.invites, hasLength(1));

      resetInvites(store);
      expect(store.value.invites, isEmpty);
      clearParty(store);
      expect(store.value.party, isNull);
    });
  });

  group('device store', () {
    test('unavailable clears state both ways', () {
      final store = createDeviceStore();
      setUnavailable(store, false);
      setSelfRegistered(store, true);
      setDevices(store, [
        const DeviceEntry(
          deviceId: 'd1',
          platform: 'android',
          deviceName: 'n',
          appVersion: '1',
          osVersion: '14',
          registeredAt: 1,
          isActive: true,
        ),
      ]);
      expect(store.value.devices, hasLength(1));

      setUnavailable(store, true);
      expect(store.value.unavailable, isTrue);
      expect(store.value.devices, isEmpty);
      expect(store.value.selfRegistered, isFalse);

      setUnavailable(store, false);
      expect(store.value.unavailable, isFalse);
    });
  });

  group('typing and presence', () {
    test('typing TTL expiry', () {
      final store = createTypingStore();
      setTyping(store, 'k', 'alice', 1000);
      setTyping(store, 'k', 'bob', 2000); // older than the TTL
      setTyping(store, 'k', 'alice', 9500); // refresh
      clearTyping(store, 'k', 'ghost'); // no-op
      expect(typingUsersOf(store.value, 'k', 10000), ['alice']);
      clearTyping(store, 'k', 'alice');
      expect(typingUsersOf(store.value, 'k', 10000), isEmpty);
    });

    test('presence staleness renders offline', () {
      final store = createPresenceStore();
      setPresence(store, 'alice', PresenceStatus.ONLINE, '', 1000);
      expect(presenceOf(store.value, 'alice')!.status, PresenceStatus.ONLINE);
      expect(presenceFresh(store.value, 'alice', 1000 + presenceTtlMs - 1),
          isTrue);
      expect(presenceFresh(store.value, 'alice', 1000 + presenceTtlMs + 1),
          isFalse);
      expect(presenceFresh(store.value, 'ghost', 1000), isFalse);
    });
  });

  test('private channel keys are order-independent', () {
    expect(privateKey('b', 'a'), privateKey('a', 'b'));
    expect(privateKey('b', 'a'), 'p:a|b');
    expect(groupKeyOf('g1'), 'g:g1');
    expect(conversationOf('p:a|b'),
        (kind: ConversationKind.private, channelId: 'a|b'));
    expect(channelTypeOf(ConversationKind.private), ChannelType.PRIVATE);
    expect(channelTypeOf(ConversationKind.group), ChannelType.GUILD);
  });
}
