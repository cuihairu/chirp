import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import Messages from './Messages';
import { jsonResponse, stubFetch } from '../test-utils';

const RECENT = [
  { id: 'm1', content: 'Hello everyone!', author: 'User1', channel: 'general', timestamp: '2024-03-18 10:30', reactions: 5 },
  { id: 'm2', content: 'Welcome to the server!', author: 'Admin', channel: 'general', timestamp: '2024-03-18 10:31', reactions: 0 },
];

describe('Messages page', () => {
  let consoleSpy: ReturnType<typeof vi.spyOn>;
  beforeEach(() => {
    consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
  });
  afterEach(() => {
    consoleSpy.mockRestore();
    vi.unstubAllGlobals();
  });

  it('renders the recent messages returned by the API', async () => {
    stubFetch((url) =>
      url.startsWith('/api/messages?limit=50') ? jsonResponse(RECENT) : jsonResponse({ results: [] }),
    );
    render(<Messages />);

    expect(await screen.findByText('Hello everyone!')).toBeTruthy();
    expect(screen.getByText('User1')).toBeTruthy();
    // Zero-reaction messages hide the reactions chip.
    expect(screen.getByText('5 reactions')).toBeTruthy();
    expect(screen.queryByText('0 reactions')).toBeNull();
  });

  it('searches on button click and renders the results', async () => {
    const fetchMock = stubFetch((url) => {
      if (url.startsWith('/api/messages?limit=50')) return jsonResponse(RECENT);
      if (url.startsWith('/api/messages/search')) {
        return jsonResponse({
          results: [{ id: 's1', content: 'found it', author: 'Seeker', channel: 'general', timestamp: '2024-03-19 09:00', reactions: 1 }],
        });
      }
      return jsonResponse({ results: [] });
    });
    render(<Messages />);
    await screen.findByText('Hello everyone!');

    fireEvent.change(screen.getByPlaceholderText('Search messages...'), {
      target: { value: 'hello world' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Search' }));

    expect(await screen.findByText('found it')).toBeTruthy();
    const searchCall = fetchMock.mock.calls.find(([u]) => String(u).includes('/api/messages/search'));
    expect(String(searchCall?.[0])).toBe('/api/messages/search?q=hello%20world');
    expect(screen.queryByText('Hello everyone!')).toBeNull();
  });

  it('also triggers the search with Enter in the input', async () => {
    const fetchMock = stubFetch((url) => {
      if (url.startsWith('/api/messages?limit=50')) return jsonResponse(RECENT);
      if (url.startsWith('/api/messages/search')) return jsonResponse({ results: [] });
      return jsonResponse({ results: [] });
    });
    render(<Messages />);
    await screen.findByText('Hello everyone!');

    fireEvent.change(screen.getByPlaceholderText('Search messages...'), { target: { value: 'x' } });
    // charCode/keyCode accompany a real Enter keypress; without them jsdom's
    // event never reaches React's onKeyPress dispatch.
    fireEvent.keyPress(screen.getByPlaceholderText('Search messages...'), {
      key: 'Enter', charCode: 13, keyCode: 13,
    });

    await waitFor(() => {
      expect(fetchMock.mock.calls.some(([u]) => String(u).includes('/api/messages/search'))).toBe(true);
    });
    // Empty result set replaces the list.
    expect(screen.queryByText('Hello everyone!')).toBeNull();
  });

  it('keeps the current list when the search request fails', async () => {
    stubFetch((url) => {
      if (url.startsWith('/api/messages?limit=50')) return jsonResponse(RECENT);
      if (url.startsWith('/api/messages/search')) throw new TypeError('search down');
      return jsonResponse({ results: [] });
    });
    render(<Messages />);
    await screen.findByText('Hello everyone!');

    fireEvent.change(screen.getByPlaceholderText('Search messages...'), { target: { value: 'x' } });
    fireEvent.click(screen.getByRole('button', { name: 'Search' }));

    await waitFor(() => {
      expect(consoleSpy).toHaveBeenCalled();
    });
    expect(screen.getByText('Hello everyone!')).toBeTruthy();
  });

  it('falls back to the five demo messages when the initial load fails', async () => {
    stubFetch(() => {
      throw new TypeError('network down');
    });
    render(<Messages />);

    await waitFor(() => {
      expect(screen.getByText('Who wants to play some games?')).toBeTruthy();
    });
    expect(screen.getByText('Admin')).toBeTruthy();
  });
});
