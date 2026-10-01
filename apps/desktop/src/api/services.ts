import { createContext, useContext } from 'react';
import { ChirpClient } from '@chirp/protocol/chirp_client';
import { asConnection, ChatApi, type ChatConnection } from './chat_api';
import { SocialApi } from './social_api';
import { PartyApi } from './party_api';
import { VoiceApi } from './voice_api';
import { createAuthStore, type AuthState } from '../state/auth_store';
import { createConversationStore, type ConversationState } from '../state/conversation_store';
import { createMessageStore, type MessageState } from '../state/message_store';
import { createTypingStore, type TypingState } from '../state/typing_store';
import { createPresenceStore, type PresenceState } from '../state/presence_store';
import { createFriendStore, type FriendState } from '../state/friend_store';
import { createPartyStore, type PartyState } from '../state/party_store';
import { createVoiceStore, type VoiceState } from '../state/voice_store';
import {
  createOnlineDevicesStore,
  type OnlineDevicesState,
} from '../state/online_devices_store';
import type { Store } from '../state/store';

/**
 * 桌面端服务图：主连接走 App 平面边缘（chirp_app_sdk_gateway 的 WS 口），
 * social / party / voice 是可降级平面——任一缺席或宕机时聊天主线照常，
 * 对应功能面隐藏（与 web_companion 同一拓扑语义；设备推送注册面是移动端
 * 语义，桌面端不接）。
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
  /** Voice-plane connection; null when voice is not configured. */
  voice: ChatConnection | null;
  /** null when voice is not configured; goes inert when voice is down. */
  voiceApi: VoiceApi | null;
  auth: Store<AuthState>;
  conversations: Store<ConversationState>;
  messages: Store<MessageState>;
  typing: Store<TypingState>;
  presence: Store<PresenceState>;
  friends: Store<FriendState>;
  partyState: Store<PartyState>;
  voiceState: Store<VoiceState>;
  /** 多端在线：本账号其他在线端清单（ rides 主连接，无需独立平面）。 */
  onlineDevices: Store<OnlineDevicesState>;
}

/** 登录页采集的服务器基址（host[:chatPort]，缺省端口见 PORTS）。 */
export const PORTS = {
  chat: 5201, // chirp_app_sdk_gateway WS
  social: 8001,
  party: 7501,
  voice: 9001,
} as const;

export function chatWsUrl(host: string): string {
  return `ws://${host.includes(':') ? host : `${host}:${PORTS.chat}`}`;
}

/** 实验平面与主网关同主机、各按约定端口（host 里的端口是网关的，剥掉）。 */
export function planeWsUrl(host: string, port: number): string {
  const hostname = host.split(':')[0] || '127.0.0.1';
  return `ws://${hostname}:${port}`;
}

export function createServices(
  options: {
    /** 服务器基址，如 `127.0.0.1:5201` 或裸 host。 */
    host?: string;
    url?: string;
    conn?: ChatConnection;
    socialUrl?: string;
    socialConn?: ChatConnection;
    partyUrl?: string;
    partyConn?: ChatConnection;
    voiceUrl?: string;
    voiceConn?: ChatConnection;
  } = {},
): Services {
  const host = options.host ?? '127.0.0.1:5201';
  const client: ChatConnection =
    options.conn ?? new ChirpClient({ url: options.url ?? chatWsUrl(host) });
  const auth = createAuthStore();
  const conversations = createConversationStore();
  const messages = createMessageStore();
  const typing = createTypingStore();
  const presence = createPresenceStore();
  const friends = createFriendStore();
  const partyState = createPartyStore();
  const voiceState = createVoiceStore();
  const onlineDevices = createOnlineDevicesStore();
  const api = new ChatApi({
    conn: asConnection(client),
    auth,
    conversations,
    messages,
    typing,
    onlineDevices,
  });

  // 与 web_companion 一致：显式传入 conn 的调用方（测试）默认 chat-only；
  // 真实部署默认把三个实验平面都拉起来，宕机各自静默降级。
  const socialConn: ChatConnection | null =
    options.socialConn ??
    (options.conn ? null : new ChirpClient({ url: options.socialUrl ?? planeWsUrl(host, PORTS.social) }));
  const socialApi = socialConn
    ? new SocialApi({ conn: asConnection(socialConn), auth, presence, friends })
    : null;
  const partyConn: ChatConnection | null =
    options.partyConn ??
    (options.conn ? null : new ChirpClient({ url: options.partyUrl ?? planeWsUrl(host, PORTS.party) }));
  const partyApi = partyConn
    ? new PartyApi({ conn: asConnection(partyConn), auth, party: partyState })
    : null;
  const voiceConn: ChatConnection | null =
    options.voiceConn ??
    (options.conn ? null : new ChirpClient({ url: options.voiceUrl ?? planeWsUrl(host, PORTS.voice) }));
  const voiceApi = voiceConn
    ? new VoiceApi({ conn: asConnection(voiceConn), auth, voice: voiceState })
    : null;

  return {
    client,
    api,
    social: socialConn,
    socialApi,
    party: partyConn,
    partyApi,
    voice: voiceConn,
    voiceApi,
    auth,
    conversations,
    messages,
    typing,
    presence,
    friends,
    partyState,
    voiceState,
    onlineDevices,
  };
}

const ServicesContext = createContext<Services | null>(null);
export const ServicesProvider = ServicesContext.Provider;

export function useServices(): Services {
  const services = useContext(ServicesContext);
  if (!services) throw new Error('ServicesProvider is missing from the tree');
  return services;
}
