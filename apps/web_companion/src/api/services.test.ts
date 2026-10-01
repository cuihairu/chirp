import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  resolveChatWsUrl,
  resolveDeviceWsUrl,
  resolvePartyWsUrl,
  resolveSocialWsUrl,
  resolveVoiceWsUrl,
} from './services';

const resolvers = [
  ['chat', 'VITE_CHAT_WS_URL', resolveChatWsUrl],
  ['social', 'VITE_SOCIAL_WS_URL', resolveSocialWsUrl],
  ['party', 'VITE_PARTY_WS_URL', resolvePartyWsUrl],
  ['voice', 'VITE_VOICE_WS_URL', resolveVoiceWsUrl],
  ['device', 'VITE_DEVICE_WS_URL', resolveDeviceWsUrl],
] as const;

describe('resolve*WsUrl', () => {
  afterEach(() => {
    vi.unstubAllEnvs();
    vi.unstubAllGlobals();
  });

  it('returns the VITE_*_WS_URL override verbatim for every plane', () => {
    for (const [, envKey, resolve] of resolvers) {
      vi.stubEnv(envKey, 'wss://override.example/plane');
      expect(resolve()).toBe('wss://override.example/plane');
      vi.unstubAllEnvs();
    }
  });

  it('derives ws://<host>/ws/<plane> from an http page with no override', () => {
    for (const [plane, , resolve] of resolvers) {
      expect(resolve()).toBe(`ws://${window.location.host}/ws/${plane}`);
    }
  });

  it('derives wss://<host>/ws/<plane> on an https page', () => {
    // jsdom's location is stubbable at the global binding; the resolvers only
    // read protocol/host off it, so a plain object stands in for the page URL.
    vi.stubGlobal('location', { protocol: 'https:', host: 'secure.example' });
    for (const [plane, , resolve] of resolvers) {
      expect(resolve()).toBe(`wss://secure.example/ws/${plane}`);
    }
  });
});
