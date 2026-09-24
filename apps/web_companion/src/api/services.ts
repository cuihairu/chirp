import { createContext, useContext } from 'react';
import { ChirpClient } from '@chirp/protocol/chirp_client';
import { asConnection, ChatApi, type ChatConnection } from './chat_api';
import { SocialApi } from './social_api';
import { PartyApi } from './party_api';
import { DeviceApi } from './device_api';
import { createAuthStore, type AuthState } from '../state/auth_store';
import { createConversationStore, type ConversationState } from '../state/conversation_store';
import { createMessageStore, type MessageState } from '../state/message_store';
import { createTypingStore, type TypingState } from '../state/typing_store';
import { createPresenceStore, type PresenceState } from '../state/presence_store';
import { createFriendStore, type FriendState } from '../state/friend_store';
import { createPartyStore, type PartyState } from '../state/party_store';
import { createDeviceStore, type DeviceState } from '../state/device_store';
import type { Store } from '../state/store';

/**
 * One object graph per browser tab: the chat websocket + stores + ChatApi,
 * plus the optional social / party / device planes (second/third/fourth
 * websockets) that are degradeable — when any is absent or down, chat works
 * and its features hide. Tests inject fake connections via
 * `createServices({ conn, socialConn, partyConn, deviceConn })`.
 */
export interface Services {
  client: ChatConnection;
  api: ChatApi;
  /** Social-plane connection; null when social is not configured. */
  social: ChatConnection | null;
  /** null when social is not configured; goes inert when social is down. */
  socialApi: SocialApi | null;
  /** Party-plane connection; null when party is not configured. */
  party: ChatConnection | null;
  /** null when party is not configured; goes inert when party is down. */
  partyApi: PartyApi | null;
  /** Device-plane connection (app_gateway); null when not configured. */
  device: ChatConnection | null;
  /** null when the device plane is not configured; inert when it is down. */
  deviceApi: DeviceApi | null;
  auth: Store<AuthState>;
  conversations: Store<ConversationState>;
  messages: Store<MessageState>;
  typing: Store<TypingState>;
  presence: Store<PresenceState>;
  friends: Store<FriendState>;
  partyState: Store<PartyState>;
  devices: Store<DeviceState>;
}

/**
 * Dev defaults ride the vite proxy (same-origin, no CORS to think about);
 * VITE_CHAT_WS_URL / VITE_SOCIAL_WS_URL / VITE_PARTY_WS_URL override for
 * direct-backend runs.
 */
export function resolveChatWsUrl(): string {
  const fromEnv = import.meta.env.VITE_CHAT_WS_URL;
  if (fromEnv) return fromEnv;
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${scheme}://${window.location.host}/ws/chat`;
}

export function resolveSocialWsUrl(): string {
  const fromEnv = import.meta.env.VITE_SOCIAL_WS_URL;
  if (fromEnv) return fromEnv;
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${scheme}://${window.location.host}/ws/social`;
}

export function resolvePartyWsUrl(): string {
  const fromEnv = import.meta.env.VITE_PARTY_WS_URL;
  if (fromEnv) return fromEnv;
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${scheme}://${window.location.host}/ws/party`;
}

export function resolveDeviceWsUrl(): string {
  const fromEnv = import.meta.env.VITE_DEVICE_WS_URL;
  if (fromEnv) return fromEnv;
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${scheme}://${window.location.host}/ws/device`;
}

export function createServices(
  options: {
    url?: string;
    conn?: ChatConnection;
    socialUrl?: string;
    socialConn?: ChatConnection;
    partyUrl?: string;
    partyConn?: ChatConnection;
    deviceUrl?: string;
    deviceConn?: ChatConnection;
  } = {},
): Services {
  const client: ChatConnection =
    options.conn ?? new ChirpClient({ url: options.url ?? resolveChatWsUrl() });
  const auth = createAuthStore();
  const conversations = createConversationStore();
  const messages = createMessageStore();
  const typing = createTypingStore();
  const presence = createPresenceStore();
  const friends = createFriendStore();
  const partyState = createPartyStore();
  const devices = createDeviceStore();
  const api = new ChatApi({ conn: asConnection(client), auth, conversations, messages, typing });

  // Social defaults ON in production (a real chat ChirpClient implies a real
  // deployment); tests that inject a chat fake get chat-only unless they also
  // inject a social fake or an explicit socialUrl. Same for the party and
  // device planes.
  const socialConn: ChatConnection | null =
    options.socialConn ??
    (options.conn ? null : new ChirpClient({ url: options.socialUrl ?? resolveSocialWsUrl() }));
  const socialApi = socialConn
    ? new SocialApi({ conn: asConnection(socialConn), auth, presence, friends })
    : null;
  const partyConn: ChatConnection | null =
    options.partyConn ??
    (options.conn ? null : new ChirpClient({ url: options.partyUrl ?? resolvePartyWsUrl() }));
  const partyApi = partyConn
    ? new PartyApi({ conn: asConnection(partyConn), auth, party: partyState })
    : null;
  const deviceConn: ChatConnection | null =
    options.deviceConn ??
    (options.conn ? null : new ChirpClient({ url: options.deviceUrl ?? resolveDeviceWsUrl() }));
  const deviceApi = deviceConn
    ? new DeviceApi({ conn: asConnection(deviceConn), auth, devices })
    : null;

  return {
    client,
    api,
    social: socialConn,
    socialApi,
    party: partyConn,
    partyApi,
    device: deviceConn,
    deviceApi,
    auth,
    conversations,
    messages,
    typing,
    presence,
    friends,
    partyState,
    devices,
  };
}

const ServicesContext = createContext<Services | null>(null);
export const ServicesProvider = ServicesContext.Provider;

export function useServices(): Services {
  const services = useContext(ServicesContext);
  if (!services) throw new Error('ServicesProvider is missing from the tree');
  return services;
}
