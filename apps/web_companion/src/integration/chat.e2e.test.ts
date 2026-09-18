import { ChannelType, GroupMemberJoinedNotify, MessageAck, MessageReadNotify, MsgType } from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ChirpClient } from '../protocol/chirp_client';
import {
  CREATE_GROUP,
  GET_GROUP_MEMBERS,
  GET_HISTORY,
  GET_UNREAD_COUNT,
  INVITE_TO_GROUP,
  LEAVE_GROUP,
  MARK_READ,
  SEND_MESSAGE,
} from '../protocol/msg_map';
import { decodeChatMessage, loginClient, nextNotify, privateChannelId } from './helpers';

/**
 * Real-backend round-trips against chirp_chat. Skipped entirely unless
 * CHIRP_WS_URL is set — run scripts/web_smoke.sh, which starts the server
 * and points this suite at it.
 */
const suite = process.env.CHIRP_WS_URL ? describe : describe.skip;

// Unique run suffix: the server keeps per-user state for the process
// lifetime, and a stale run's groups/unreads must not leak into assertions.
const RUN = Date.now().toString(36);
const A = `web_e2e_a_${RUN}`;
const B = `web_e2e_b_${RUN}`;
const enc = (text: string): Uint8Array => new TextEncoder().encode(text);
const dec = (bytes: Uint8Array): string => new TextDecoder().decode(bytes);

suite('chat integration (CHIRP_WS_URL)', () => {
  let userA: ChirpClient;
  let userB: ChirpClient;

  beforeAll(async () => {
    userA = await loginClient(A, `dev-a-${RUN}`);
    userB = await loginClient(B, `dev-b-${RUN}`);
  });

  afterAll(() => {
    userA?.disconnect();
    userB?.disconnect();
  });

  it('delivers a private message live to the online peer', async () => {
    const received = nextNotify(userB, MsgID.CHAT_MESSAGE_NOTIFY, decodeChatMessage, (m) => m.senderId === A);

    const resp = await userA.request(SEND_MESSAGE, {
      senderId: A,
      receiverId: B,
      channelType: ChannelType.PRIVATE,
      msgType: MsgType.TEXT,
      content: enc('e2e-hello-1'),
      clientTimestamp: Date.now(),
    });
    expect(resp.code).toBe(0);
    expect(resp.messageId).toBeTruthy();

    // The live push and the sender's response carry the same server id; the
    // sender gets no echo of its own message.
    const pushed = await received;
    expect(pushed.messageId).toBe(resp.messageId);
    expect(dec(pushed.content)).toBe('e2e-hello-1');

    // Acknowledge the delivery so the server clears its pending tracker.
    expect(() =>
      userB.send(
        MsgID.MESSAGE_ACK,
        MessageAck.encode(MessageAck.fromPartial({ messageId: pushed.messageId, userId: B, receivedAt: Date.now() })).finish(),
      ),
    ).not.toThrow();
  });

  it('pages private history with before_timestamp', async () => {
    const channel = privateChannelId(A, B);
    for (const text of ['e2e-page-1', 'e2e-page-2', 'e2e-page-3']) {
      const resp = await userA.request(SEND_MESSAGE, {
        senderId: A,
        receiverId: B,
        channelType: ChannelType.PRIVATE,
        msgType: MsgType.TEXT,
        content: enc(text),
        clientTimestamp: Date.now(),
      });
      expect(resp.code).toBe(0);
    }

    const page1 = await userA.request(GET_HISTORY, {
      userId: A,
      channelType: ChannelType.PRIVATE,
      channelId: channel,
      limit: 2,
    });
    expect(page1.code).toBe(0);
    expect(page1.messages.length).toBe(2);
    expect(page1.hasMore).toBe(true);

    const page2 = await userA.request(GET_HISTORY, {
      userId: A,
      channelType: ChannelType.PRIVATE,
      channelId: channel,
      limit: 2,
      beforeTimestamp: page1.messages[0].timestamp,
    });
    expect(page2.code).toBe(0);
    expect(page2.messages.length).toBeGreaterThanOrEqual(2);
    // Pages never overlap: everything on page 2 is older than page 1's head.
    for (const msg of page2.messages) {
      expect(msg.timestamp).toBeLessThan(page1.messages[0].timestamp);
    }
  });

  it('answers unread queries and broadcasts read receipts', async () => {
    const channel = privateChannelId(A, B);

    // NOTE: the server-side unread counter has no increment path today
    // (neither the in-memory ReadReceiptManager nor the MySQL schema writes
    // unread_count on delivery), so GET_UNREAD_COUNT always reports 0.
    // Until the backend fills that in, this case only pins the API contract:
    // the query answers OK, and the peer receives the read receipt.
    const before = await userB.request(GET_UNREAD_COUNT, { userId: B });
    expect(before.code).toBe(0);
    expect(before.totalUnread).toBe(0);

    // A must see B's read receipt.
    const readNotify = nextNotify(userA, MsgID.MESSAGE_READ_NOTIFY, MessageReadNotify.decode, (n) => n.readerUserId === B);
    const mark = await userB.request(MARK_READ, {
      userId: B,
      channelType: ChannelType.PRIVATE,
      channelId: channel,
      messageId: '',
      readTimestamp: Date.now(),
    });
    expect(mark.code).toBe(0);
    const receipt = await readNotify;
    expect(receipt.channelId).toBe(channel);

    const after = await userB.request(GET_UNREAD_COUNT, { userId: B });
    expect(after.code).toBe(0);
  });

  it('runs the group lifecycle: create, invite, live notify, group send, history, leave', async () => {
    // Create — the inviter owns the group.
    const created = await userA.request(CREATE_GROUP, {
      creatorId: A,
      groupName: `e2e-group-${RUN}`,
      description: 'web companion integration',
    });
    expect(created.code).toBe(0);
    const groupId = created.groupId;
    expect(groupId).toBeTruthy();

    // Invite — server semantics: the target joins immediately and everyone
    // (including the joiner) learns it through GROUP_MEMBER_JOINED_NOTIFY.
    const joined = nextNotify(userB, MsgID.GROUP_MEMBER_JOINED_NOTIFY, GroupMemberJoinedNotify.decode, (n) => n.groupId === groupId);
    const invited = await userA.request(INVITE_TO_GROUP, {
      inviterId: A,
      groupId,
      targetUserId: B,
    });
    expect(invited.code).toBe(0);
    const joinNotify = await joined;
    expect(joinNotify.member?.userId).toBe(B);

    const members = await userA.request(GET_GROUP_MEMBERS, { groupId });
    expect(members.code).toBe(0);
    expect(members.totalCount).toBe(2);

    // Group send: channel_type GUILD, channel_id = group id, receiver empty.
    const groupMsg = nextNotify(userB, MsgID.CHAT_MESSAGE_NOTIFY, decodeChatMessage, (m) => m.senderId === A && m.channelId === groupId);
    const sent = await userA.request(SEND_MESSAGE, {
      senderId: A,
      channelType: ChannelType.GUILD,
      channelId: groupId,
      msgType: MsgType.TEXT,
      content: enc('e2e-group-hello'),
      clientTimestamp: Date.now(),
    });
    expect(sent.code).toBe(0);
    const pushed = await groupMsg;
    expect(dec(pushed.content)).toBe('e2e-group-hello');

    const history = await userB.request(GET_HISTORY, {
      userId: B,
      channelType: ChannelType.GUILD,
      channelId: groupId,
      limit: 10,
    });
    expect(history.code).toBe(0);
    expect(history.messages.some((m) => dec(m.content) === 'e2e-group-hello')).toBe(true);

    // Leave — the remaining roster shrinks back to the owner.
    const left = await userB.request(LEAVE_GROUP, { userId: B, groupId });
    expect(left.code).toBe(0);
    const after = await userA.request(GET_GROUP_MEMBERS, { groupId });
    expect(after.totalCount).toBe(1);
  });

  it('answers SEND_MESSAGE_RESP with a non-OK code for a channel the sender cannot post in', async () => {
    // A group the sender does not belong to must not be sendable.
    const created = await userA.request(CREATE_GROUP, {
      creatorId: A,
      groupName: `e2e-reject-${RUN}`,
      description: '',
    });
    expect(created.code).toBe(0);
    const resp = await userB.request(SEND_MESSAGE, {
      senderId: B,
      channelType: ChannelType.GUILD,
      channelId: created.groupId,
      msgType: MsgType.TEXT,
      content: enc('should not pass'),
      clientTimestamp: Date.now(),
    });
    // The server must answer with a decoded response (any non-OK code),
    // never drop the request into a timeout.
    expect(resp.code).not.toBe(0);
  });
});
