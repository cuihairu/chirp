import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import Users from './Users';
import { jsonResponse, stubFetch } from '../test-utils';

const USERS = [
  { id: 'u1', username: 'Ada', email: 'ada@example.com', status: 'online', role: 'admin', joinedAt: '2024-01-01', lastSeen: '2024-03-01T10:00:00Z', messagesSent: 10 },
  { id: 'u2', username: 'Bob', email: 'bob@example.com', status: 'away', role: 'user', joinedAt: '2024-01-02', lastSeen: '2024-03-02T10:00:00Z', messagesSent: 20 },
  { id: 'u3', username: 'Grace', email: 'grace@example.com', status: 'offline', role: 'moderator', joinedAt: '2024-01-03', lastSeen: '2024-03-03T10:00:00Z', messagesSent: 30 },
];

const dataRows = () => screen.getAllByRole('row').length - 1; // minus header row

describe('Users page', () => {
  let consoleSpy: ReturnType<typeof vi.spyOn>;
  beforeEach(() => {
    consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
  });
  afterEach(() => {
    consoleSpy.mockRestore();
    vi.unstubAllGlobals();
  });

  it('renders the users returned by the API', async () => {
    stubFetch((url) => (url === '/api/users' ? jsonResponse(USERS) : jsonResponse([])));
    render(<Users />);

    expect(await screen.findByText('Ada')).toBeTruthy();
    expect(screen.getByText('Bob')).toBeTruthy();
    expect(screen.getByText('Grace')).toBeTruthy();
    expect(dataRows()).toBe(3);
  });

  it('narrows the table as the search query matches fewer users', async () => {
    stubFetch((url) => (url === '/api/users' ? jsonResponse(USERS) : jsonResponse([])));
    render(<Users />);
    await screen.findByText('Ada');

    const search = screen.getByPlaceholderText('Search users...');
    fireEvent.change(search, { target: { value: 'grace' } });
    expect(screen.queryByText('Ada')).toBeNull();
    expect(screen.getByText('Grace')).toBeTruthy();

    fireEvent.change(search, { target: { value: 'zzz nobody' } });
    expect(dataRows()).toBe(0);
  });

  it('falls back to the 50-row demo list when the API is unreachable', async () => {
    stubFetch(() => {
      throw new TypeError('network down');
    });
    render(<Users />);

    await waitFor(() => {
      expect(consoleSpy).toHaveBeenCalled();
    });
    await waitFor(() => {
      expect(dataRows()).toBe(50);
    });
  });

  it('opens the row action menu and closes it after an action', async () => {
    stubFetch((url) => (url === '/api/users' ? jsonResponse(USERS) : jsonResponse([])));
    const logSpy = vi.spyOn(console, 'log').mockImplementation(() => {});
    render(<Users />);
    await screen.findByText('Ada');

    fireEvent.click(screen.getAllByRole('button')[0]);
    expect(screen.getByText('Kick from Server')).toBeTruthy();
    expect(screen.getByText('Ban User')).toBeTruthy();

    fireEvent.click(screen.getByText('Ban User'));
    expect(logSpy).toHaveBeenCalledWith('Ban user:', 'u1');
    // The exit transition unmounts the menu a tick later; wait for it.
    await waitFor(() => {
      expect(screen.queryByText('Kick from Server')).toBeNull();
    });
    logSpy.mockRestore();
  });
});
