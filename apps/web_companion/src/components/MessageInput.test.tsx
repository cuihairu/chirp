import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import MessageInput from './MessageInput';

describe('MessageInput emoji picker (P1 表情/贴图)', () => {
  it('keeps the picker closed by default and opens it from the toggle', () => {
    render(<MessageInput onSend={vi.fn()} />);
    expect(screen.queryByTestId('emoji-pick-smile')).toBeNull();
    fireEvent.click(screen.getByTestId('emoji-picker-toggle'));
    expect(screen.getByTestId('emoji-pick-smile')).toBeTruthy();
  });

  it('inserts the picked emoji code into the draft', () => {
    render(<MessageInput onSend={vi.fn()} />);
    fireEvent.click(screen.getByTestId('emoji-picker-toggle'));
    fireEvent.click(screen.getByTestId('emoji-pick-fire'));
    expect((screen.getByRole('textbox') as HTMLTextAreaElement).value).toContain('[emoji:fire]');
  });

  it('sends the inserted emoji code through as text', () => {
    const onSend = vi.fn();
    render(<MessageInput onSend={onSend} />);
    fireEvent.click(screen.getByTestId('emoji-picker-toggle'));
    fireEvent.click(screen.getByTestId('emoji-pick-smile'));
    fireEvent.click(screen.getByRole('button', { name: 'send' }));
    expect(onSend).toHaveBeenCalledWith('[emoji:smile]');
  });
});
