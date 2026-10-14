import { act, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import MarqueeStrip from './MarqueeStrip';

describe('MarqueeStrip', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('shows the banner content at the top strip', () => {
    render(<MarqueeStrip content="全服维护 30 分钟" ttlSeconds={30} onExpire={() => undefined} />);
    expect(screen.getByTestId('marquee-strip')).toBeTruthy();
    expect(screen.getByText('全服维护 30 分钟')).toBeTruthy();
  });

  it('collapses after the ttl', () => {
    const onExpire = vi.fn();
    render(<MarqueeStrip content="维护" ttlSeconds={10} onExpire={onExpire} />);
    act(() => {
      vi.advanceTimersByTime(9_999);
    });
    expect(onExpire).not.toHaveBeenCalled();
    act(() => {
      vi.advanceTimersByTime(1);
    });
    expect(onExpire).toHaveBeenCalledTimes(1);
  });

  it('restarts the ttl when a new banner replaces the current one', () => {
    const onExpire = vi.fn();
    const { rerender } = render(
      <MarqueeStrip content="旧公告" ttlSeconds={10} onExpire={onExpire} />,
    );
    act(() => {
      vi.advanceTimersByTime(9_000);
    });
    rerender(<MarqueeStrip content="新公告" ttlSeconds={5} onExpire={onExpire} />);
    act(() => {
      vi.advanceTimersByTime(4_999);
    });
    expect(onExpire).not.toHaveBeenCalled();
    expect(screen.getByText('新公告')).toBeTruthy();
    act(() => {
      vi.advanceTimersByTime(1);
    });
    expect(onExpire).toHaveBeenCalledTimes(1);
  });
});