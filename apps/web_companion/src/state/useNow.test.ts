import { act } from '@testing-library/react';
import { renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { useNow } from './useNow';

describe('useNow', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('captures the clock on mount without ticking', () => {
    vi.setSystemTime(1000);
    const { result } = renderHook(() => useNow(15_000));
    expect(result.current).toBe(1000);
  });

  it('refreshes on the interval and cleans up after unmount', () => {
    vi.setSystemTime(1000);
    const { result, unmount } = renderHook(() => useNow(15_000));
    act(() => {
      vi.advanceTimersByTime(15_000);
    });
    expect(result.current).toBe(16_000);
    unmount();
    act(() => {
      vi.advanceTimersByTime(30_000);
    });
    expect(result.current).toBe(16_000);
  });
});
