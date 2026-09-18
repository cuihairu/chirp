import { ChatMessage } from '@chirp/proto/chat';
import { MsgID } from '@chirp/proto/gateway';
import { ChirpClient } from '../protocol/chirp_client';
import { LOGIN } from '../protocol/msg_map';

/**
 * Shared helpers for the CHIRP_WS_URL-gated integration suite run by
 * scripts/web_smoke.sh against a real chirp_chat process.
 */

/** Connect + LOGIN (scaffold mode: token = user id) + reset the backoff. */
export async function loginClient(userId: string, deviceId: string): Promise<ChirpClient> {
  const client = new ChirpClient({ url: process.env.CHIRP_WS_URL as string });
  await client.connect();
  const resp = await client.request(LOGIN, {
    token: userId,
    deviceId,
    platform: 'web',
    supportsMessageAck: true,
  });
  if (resp.code !== 0) {
    client.disconnect();
    throw new Error(`login failed for ${userId}: code=${resp.code}`);
  }
  client.resetBackoff();
  return client;
}

/** Resolve the next notify of `msgId` whose decoded body matches. */
export function nextNotify<T>(
  client: ChirpClient,
  msgId: MsgID,
  decode: (body: Uint8Array) => T,
  match: (decoded: T) => boolean,
  timeoutMs = 8000,
): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => {
      off?.();
      reject(new Error(`timeout waiting for notify ${msgId}`));
    }, timeoutMs);
    const off = client.onNotify(msgId, (body) => {
      let decoded: T;
      try {
        decoded = decode(body);
      } catch {
        return; // unrelated or malformed push
      }
      if (!match(decoded)) return;
      clearTimeout(timer);
      off?.();
      resolve(decoded);
    });
  });
}

export const decodeChatMessage = (body: Uint8Array): ChatMessage => ChatMessage.decode(body);

/** The private-history channel id the server uses: the two ids, sorted, '|'-joined. */
export const privateChannelId = (a: string, b: string): string => [a, b].sort().join('|');
