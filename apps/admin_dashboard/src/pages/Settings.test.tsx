import { describe, it, expect, vi, afterEach } from 'vitest';
import { render, screen, fireEvent, act } from '@testing-library/react';
import Settings from './Settings';

const switches = () => screen.getAllByRole('checkbox');

describe('Settings page', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it('renders every section with the default values', () => {
    render(<Settings />);

    for (const section of ['General', 'User Registration', 'Features', 'Privacy & Analytics', 'Advanced']) {
      expect(screen.getByText(section)).toBeTruthy();
    }
    expect((screen.getByLabelText('Server Name') as HTMLInputElement).value).toBe('Chirp Server');
    expect(screen.getByLabelText('Enable user registration')).toBeTruthy();
    // Six switches: registration, email verification, voice, upload, analytics, debug.
    expect(switches().length).toBe(6);
  });

  it('shows the success alert on save and hides it after three seconds', () => {
    vi.useFakeTimers();
    render(<Settings />);

    expect(screen.queryByRole('alert')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Save Changes' }));
    expect(screen.getByRole('alert').textContent).toContain('Settings saved successfully!');

    fireEvent.click(screen.getByRole('button', { name: 'Save Changes' }));
    // The 3s clear timer fires outside React's event system; act() flushes it.
    act(() => {
      vi.advanceTimersByTime(3000);
    });
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('disables email verification while registration is off', () => {
    render(<Settings />);
    const [registration, emailVerification] = switches();

    expect(emailVerification.hasAttribute('disabled')).toBe(false);
    fireEvent.click(registration);
    expect(emailVerification.hasAttribute('disabled')).toBe(true);

    // Re-enabling restores it.
    fireEvent.click(registration);
    expect(emailVerification.hasAttribute('disabled')).toBe(false);
  });

  it('hides the max-file-size field when file upload is turned off', () => {
    render(<Settings />);
    expect(screen.getByLabelText('Max File Size (MB)')).toBeTruthy();

    fireEvent.click(screen.getByLabelText('Enable file upload'));
    expect(screen.queryByLabelText('Max File Size (MB)')).toBeNull();
  });

  it('accepts edits in the text fields', () => {
    render(<Settings />);
    const serverName = screen.getByLabelText('Server Name') as HTMLInputElement;

    fireEvent.change(serverName, { target: { value: 'My Realm' } });
    expect(serverName.value).toBe('My Realm');
  });
});
