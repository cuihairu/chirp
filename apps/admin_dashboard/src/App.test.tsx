import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import App from './App';
import { jsonResponse, stubFetch } from './test-utils';

const STATS = { totalUsers: 1234567, onlineUsers: 8912, totalMessages: 55, activeChannels: 7 };
const USERS = [
  { id: 'u1', username: 'Ada', email: 'ada@example.com', status: 'online', role: 'admin', joinedAt: '2024-01-01', lastSeen: '2024-03-01', messagesSent: 10 },
  { id: 'u2', username: 'Bob', email: 'bob@example.com', status: 'offline', role: 'user', joinedAt: '2024-01-02', lastSeen: '2024-03-02', messagesSent: 20 },
];

// App mounts the Dashboard route, whose fake loading gate opens after 500ms,
// and re-polls /api/stats every 5s — fake timers keep both deterministic.
async function renderAppPastIntroGate() {
  const utils = render(<App />);
  await vi.advanceTimersByTimeAsync(500);
  return utils;
}

describe('App shell', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it('renders the shell and the dashboard behind it', async () => {
    stubFetch((url) => (url === '/api/stats' ? jsonResponse(STATS) : jsonResponse([])));
    await renderAppPastIntroGate();

    expect(screen.getByText('Chirp Admin Dashboard')).toBeTruthy();
    for (const nav of ['Dashboard', 'Users', 'Channels', 'Messages', 'Settings']) {
      expect(screen.getAllByText(nav).length).toBeGreaterThan(0);
    }
    // Stats flowed from /api/stats through App state into the metric cards.
    expect(screen.getByText('1,234,567')).toBeTruthy();
    expect(screen.getByText('Online:')).toBeTruthy();
  });

  it('re-polls /api/stats every 5 seconds', async () => {
    const fetchMock = stubFetch((url) => (url === '/api/stats' ? jsonResponse(STATS) : jsonResponse([])));
    await renderAppPastIntroGate();
    const callsAfterMount = fetchMock.mock.calls.filter(([u]) => String(u) === '/api/stats').length;
    expect(callsAfterMount).toBeGreaterThanOrEqual(1);

    await vi.advanceTimersByTimeAsync(5000);
    expect(fetchMock.mock.calls.filter(([u]) => String(u) === '/api/stats').length)
      .toBeGreaterThanOrEqual(callsAfterMount + 1);
  });

  it('keeps the shell up when /api/stats answers 200 with missing fields', async () => {
    // Regression: setStats(data) used to plant undefined into the header's
    // .toLocaleString() renders and blank the whole dashboard.
    stubFetch((url) => (url === '/api/stats' ? jsonResponse({}) : jsonResponse([])));
    await renderAppPastIntroGate();

    expect(screen.getByText('Chirp Admin Dashboard')).toBeTruthy();
    expect(screen.getByText('Online:')).toBeTruthy();
    expect(screen.getAllByText('0').length).toBeGreaterThan(0);
  });

  it('navigates to Users from the drawer and fetches the list', async () => {
    const fetchMock = stubFetch((url) => {
      if (url === '/api/stats') return jsonResponse(STATS);
      if (url === '/api/users') return jsonResponse(USERS);
      return jsonResponse([]);
    });
    await renderAppPastIntroGate();

    fireEvent.click(screen.getAllByText('Users')[0]);
    await vi.advanceTimersByTimeAsync(0);

    expect(screen.getByText('Ada')).toBeTruthy();
    expect(screen.getByText('Bob')).toBeTruthy();
    expect(fetchMock.mock.calls.some(([u]) => String(u) === '/api/users')).toBe(true);
  });
});
