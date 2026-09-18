import { createContext, useContext } from 'react';
import { ChirpClient } from '../protocol/chirp_client';
import { asConnection, ChatApi, type ChatConnection } from './chat_api';
import { createAuthStore, type AuthState } from '../state/auth_store';
import { createConversationStore, type ConversationState } from '../state/conversation_store';
import { createMessageStore, type MessageState } from '../state/message_store';
import type { Store } from '../state/store';

/**
 * One object graph per browser tab: a websocket client, the three stores and
 * the ChatApi that wires them together. Created once in App; tests inject a
 * fake connection via `createServices({ conn })`.
 */
export interface Services {
  client: ChatConnection;
  api: ChatApi;
  auth: Store<AuthState>;
  conversations: Store<ConversationState>;
  messages: Store<MessageState>;
}

/**
 * Dev default rides the vite proxy (same-origin, no CORS to think about);
 * VITE_CHAT_WS_URL overrides for direct-backend runs.
 */
export function resolveChatWsUrl(): string {
  const fromEnv = import.meta.env.VITE_CHAT_WS_URL;
  if (fromEnv) return fromEnv;
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${scheme}://${window.location.host}/ws/chat`;
}

export function createServices(options: { url?: string; conn?: ChatConnection } = {}): Services {
  const client: ChatConnection =
    options.conn ?? new ChirpClient({ url: options.url ?? resolveChatWsUrl() });
  const auth = createAuthStore();
  const conversations = createConversationStore();
  const messages = createMessageStore();
  const api = new ChatApi({ conn: asConnection(client), auth, conversations, messages });
  return { client, api, auth, conversations, messages };
}

const ServicesContext = createContext<Services | null>(null);
export const ServicesProvider = ServicesContext.Provider;

export function useServices(): Services {
  const services = useContext(ServicesContext);
  if (!services) throw new Error('ServicesProvider is missing from the tree');
  return services;
}
