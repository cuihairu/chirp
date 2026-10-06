import { ChirpClient } from '@chirp/app-protocol/chirp_client';
import { asConnection, ChatApi, type ChatConnection } from './chat_api';
import { SocialApi } from './social_api';
import { PartyApi } from './party_api';
import { VoiceApi } from './voice_api';
import { DeviceApi } from './device_api';
import { GamePresenceApi } from './game_presence_api';
import { createAuthStore, type AuthState } from '../state/auth_store';
import { createConversationStore, type ConversationState } from '../state/conversation_store';
import { createMessageStore, type MessageState } from '../state/message_store';
import { createTypingStore, type TypingState } from '../state/typing_store';
import { createPresenceStore, type PresenceState } from '../state/presence_store';
import { createFriendStore, type FriendState } from '../state/friend_store';
import { createPartyStore, type PartyState } from '../state/party_store';
import { createVoiceStore, type VoiceState } from '../state/voice_store';
import { createDeviceStore, type DeviceState } from '../state/device_store';
import {
  createOnlineDevicesStore,
  type OnlineDevicesState,
} from '../state/online_devices_store';
import {
  createGamePresenceStore,
  type GamePresenceState,
} from '../state/game_presence_store';
import type { Store } from '../state/store';

/**
 * One object graph per app process: the chat websocket + stores + ChatApi,
 * plus the optional social / party / device planes (second/third/fourth
 * websockets) that are degradeable — when any is absent or down, chat works
 * and its features hide. Ported from apps/web_companion src/api/services.ts;
 * 两个必须的平台注入:
 *  - wsFactory: 浏览器端用全局 WebSocket 默认工厂，鸿蒙没有该全局，
 *    必须传入 common/HarmonyWebSocket 的工厂（漏传会在 connect 时炸出）;
 *  - deviceId: web 端 createAuthStore 读 localStorage，鸿蒙由 HostConfig
 *    预载后传入（语义不变：每安装稳定 id，避免重连互踢）。
 * 同理没有同源默认地址（无 window.location）：五个平面 URL 全部显式传入，
 * 由 HostConfig 的服务器地址拼出。
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
  /** Device-plane connection (app_gateway); null when not configured. */
  device: ChatConnection | null;
  /** null when the device plane is not configured; inert when it is down. */
  deviceApi: DeviceApi | null;
  /** Rides the same app_gateway socket as deviceApi; null without the edge. */
  gamePresenceApi: GamePresenceApi | null;
  auth: Store<AuthState>;
  conversations: Store<ConversationState>;
  messages: Store<MessageState>;
  typing: Store<TypingState>;
  presence: Store<PresenceState>;
  friends: Store<FriendState>;
  partyState: Store<PartyState>;
  voiceState: Store<VoiceState>;
  devices: Store<DeviceState>;
  /** 多端在线（P0）：本账号其他在线端清单。 */
  onlineDevices: Store<OnlineDevicesState>;
  /** 游戏在线状态（P0）：开关与当前生效的游戏清单。 */
  gamePresence: Store<GamePresenceState>;
}

export interface CreateServicesOptions {
  /** WebSocket 工厂（必传）：鸿蒙用 common/HarmonyWebSocket 提供。 */
  wsFactory: (url: string) => import('@chirp/app-protocol/chirp_client').WebSocketLike;
  /** 持久化的每安装 device id（必传）：由 HostConfig 预载。 */
  deviceId: string;
  /** 设备展示名（必传）：平台层从 @ohos.deviceInfo 归纳。 */
  deviceName: string;
  chatUrl: string;
  socialUrl?: string;
  partyUrl?: string;
  voiceUrl?: string;
  deviceUrl?: string;
}

export function createServices(options: CreateServicesOptions): Services {
  const client: ChatConnection = new ChirpClient({
    url: options.chatUrl,
    wsFactory: options.wsFactory,
  });
  const auth = createAuthStore(options.deviceId);
  const conversations = createConversationStore();
  const messages = createMessageStore();
  const typing = createTypingStore();
  const presence = createPresenceStore();
  const friends = createFriendStore();
  const partyState = createPartyStore();
  const voiceState = createVoiceStore();
  const devices = createDeviceStore();
  const onlineDevices = createOnlineDevicesStore();
  const gamePresence = createGamePresenceStore();
  const api = new ChatApi({
    conn: asConnection(client),
    auth,
    conversations,
    messages,
    typing,
    onlineDevices,
  });

  // 与 web 端同口径：四个附属平面只有拿到显式 URL 才建（设备上没有 vite
  // 代理兜底，HostConfig 只给服务器地址时附属平面随主地址一起拼出或一起缺）。
  const socialConn: ChatConnection | null = options.socialUrl
    ? new ChirpClient({ url: options.socialUrl, wsFactory: options.wsFactory })
    : null;
  const socialApi = socialConn
    ? new SocialApi({ conn: asConnection(socialConn), auth, presence, friends })
    : null;
  const partyConn: ChatConnection | null = options.partyUrl
    ? new ChirpClient({ url: options.partyUrl, wsFactory: options.wsFactory })
    : null;
  const partyApi = partyConn
    ? new PartyApi({ conn: asConnection(partyConn), auth, party: partyState })
    : null;
  const voiceConn: ChatConnection | null = options.voiceUrl
    ? new ChirpClient({ url: options.voiceUrl, wsFactory: options.wsFactory })
    : null;
  const voiceApi = voiceConn
    ? new VoiceApi({ conn: asConnection(voiceConn), auth, voice: voiceState })
    : null;
  const deviceConn: ChatConnection | null = options.deviceUrl
    ? new ChirpClient({ url: options.deviceUrl, wsFactory: options.wsFactory })
    : null;
  const deviceApi = deviceConn
    ? new DeviceApi({ conn: asConnection(deviceConn), auth, devices, deviceName: options.deviceName })
    : null;
  // Same app_gateway socket: the presence RPCs are the edge's other
  // self-service surface (player_id pinned server-side like the device RPCs).
  const gamePresenceApi = deviceConn
    ? new GamePresenceApi({ conn: asConnection(deviceConn), auth, presence: gamePresence })
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
    device: deviceConn,
    deviceApi,
    gamePresenceApi,
    auth,
    conversations,
    messages,
    typing,
    presence,
    friends,
    partyState,
    voiceState,
    devices,
    onlineDevices,
    gamePresence,
  };
}

/** 由服务器基址（如 ws://192.168.1.5:8080）拼五平面地址的唯一入口。 */
export function planeUrls(base: string): {
  chatUrl: string;
  socialUrl: string;
  partyUrl: string;
  voiceUrl: string;
  deviceUrl: string;
} {
  const trimmed = base.replace(/\/+$/, '');
  return {
    chatUrl: `${trimmed}/ws/chat`,
    socialUrl: `${trimmed}/ws/social`,
    partyUrl: `${trimmed}/ws/party`,
    voiceUrl: `${trimmed}/ws/voice`,
    deviceUrl: `${trimmed}/ws/device`,
  };
}
