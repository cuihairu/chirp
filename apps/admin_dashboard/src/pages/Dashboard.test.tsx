import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, waitFor } from '@testing-library/react';
import Dashboard from './Dashboard';

const STATS = { totalUsers: 1234567, onlineUsers: 8912, totalMessages: 55, activeChannels: 7 };

describe('Dashboard', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('shows the progress bar while the intro gate is closed', () => {
    const { container } = render(<Dashboard stats={STATS} />);
    expect(container.querySelector('.MuiLinearProgress-root')).toBeTruthy();
    expect(screen.queryByText('Total Users')).toBeNull();
  });

  it('renders metric cards with formatted prop stats after the gate opens', async () => {
    render(<Dashboard stats={STATS} />);
    await vi.advanceTimersByTimeAsync(500);

    expect(screen.getByText('Dashboard')).toBeTruthy();
    expect(screen.getByText('Total Users')).toBeTruthy();
    expect(screen.getByText('1,234,567')).toBeTruthy();
    expect(screen.getByText('8,912')).toBeTruthy();
    expect(screen.getByText('Active Channels')).toBeTruthy();
    expect(screen.getByText('7')).toBeTruthy();
  });

  it('renders the service status table and recent alerts', async () => {
    render(<Dashboard stats={STATS} />);
    await vi.advanceTimersByTimeAsync(500);

    // Six services, all healthy
    for (const service of ['Gateway', 'Chat', 'Social', 'Voice', 'Auth', 'Notification']) {
      expect(screen.getByText(service)).toBeTruthy();
    }
    expect(screen.getByText('Redis connection timeout')).toBeTruthy();
    expect(screen.getByText('New deployment completed')).toBeTruthy();
  });

  it('draws the recharts surfaces', async () => {
    // Real timers: the stubbed ResizeObserver reports on a macrotask and the
    // fake-timer clock never reaches it deterministically (effect flush lands
    // mid-advance). waitFor absorbs the 500ms gate plus the RO tick instead.
    vi.useRealTimers();
    const { container } = render(<Dashboard stats={STATS} />);

    await screen.findByText('Total Users'); // intro gate opened
    await waitFor(() => {
      // jsdom cannot lay out the charts, but the svg surfaces must mount.
      expect(container.querySelectorAll('.recharts-surface').length).toBeGreaterThan(0);
    });

    for (const title of ['Messages per Minute', 'Channel Types', 'User Activity (24h)', 'API Latency (ms)']) {
      expect(screen.getByText(title)).toBeTruthy();
    }
  });
});
