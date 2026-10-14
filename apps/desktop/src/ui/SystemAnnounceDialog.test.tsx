import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import SystemAnnounceDialog from './SystemAnnounceDialog';

describe('SystemAnnounceDialog', () => {
  it('renders the announce body and closes on ack', () => {
    const onClose = vi.fn();
    render(<SystemAnnounceDialog content="全服奖励已发放" onClose={onClose} />);
    const dialog = screen.getByTestId('system-announce-dialog');
    expect(within(dialog).getByText('全服奖励已发放')).toBeTruthy();
    fireEvent.click(within(dialog).getByTestId('announce-ack'));
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it('keeps multi-line bodies intact', () => {
    render(<SystemAnnounceDialog content={'第一行\n第二行'} onClose={() => undefined} />);
    expect(screen.getByText(/第一行/)).toBeTruthy();
    expect(screen.getByText(/第二行/)).toBeTruthy();
  });
});