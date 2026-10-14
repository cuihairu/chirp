import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import SystemAnnounceDialog from './SystemAnnounceDialog';
import { zh } from '../i18n/zh';

describe('SystemAnnounceDialog', () => {
  it('shows the announcement content under the system title', () => {
    render(<SystemAnnounceDialog content="今晚 20:00 停机维护" onClose={() => undefined} />);
    expect(screen.getByTestId('system-announce-dialog')).toBeTruthy();
    expect(screen.getByText(zh.chat.announceTitle)).toBeTruthy();
    expect(screen.getByText('今晚 20:00 停机维护')).toBeTruthy();
  });

  it('closes on the ack button', () => {
    const onClose = vi.fn();
    render(<SystemAnnounceDialog content="全服奖励已发放" onClose={onClose} />);
    fireEvent.click(screen.getByTestId('announce-ack'));
    expect(onClose).toHaveBeenCalledTimes(1);
  });
});
