import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, waitFor } from '@testing-library/react';
import Channels from './Channels';
import { jsonResponse, stubFetch } from '../test-utils';

const CHANNELS = [
  { id: 'ch_1', name: 'general', type: 'text', category: 'General', memberCount: 1523, messageCount: 45678, createdAt: '2024-01-01' },
  { id: 'ch_2', name: 'voice-general', type: 'voice', category: 'Voice', memberCount: 45, messageCount: 0, createdAt: '2024-01-01' },
];

const dataRows = () => screen.getAllByRole('row').length - 1;

describe('Channels page', () => {
  let consoleSpy: ReturnType<typeof vi.spyOn>;
  beforeEach(() => {
    consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
  });
  afterEach(() => {
    consoleSpy.mockRestore();
    vi.unstubAllGlobals();
  });

  it('renders the channels returned by the API', async () => {
    stubFetch((url) => (url === '/api/channels' ? jsonResponse(CHANNELS) : jsonResponse([])));
    render(<Channels />);

    expect(await screen.findByText('#general')).toBeTruthy();
    expect(screen.getByText('#voice-general')).toBeTruthy();
    expect(screen.getByText('1,523')).toBeTruthy();
    expect(screen.getByText('Create Channel')).toBeTruthy();
  });

  it('keeps an empty table (header only) when the API answers an empty list', async () => {
    stubFetch((url) => (url === '/api/channels' ? jsonResponse([]) : jsonResponse([])));
    render(<Channels />);

    await waitFor(() => {
      expect(screen.getByText('Name')).toBeTruthy();
    });
    expect(dataRows()).toBe(0);
  });

  it('falls back to the five demo channels when the API is unreachable', async () => {
    stubFetch(() => {
      throw new TypeError('network down');
    });
    render(<Channels />);

    await waitFor(() => {
      expect(dataRows()).toBe(5);
    });
    expect(screen.getByText('#announcements')).toBeTruthy();
    expect(screen.getByText('#gaming')).toBeTruthy();
  });
});
