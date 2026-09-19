import { describe, expect, it } from 'vitest';
import { shouldNotifyFor } from './desktop_notify';

describe('shouldNotifyFor', () => {
  const msg = { channel: { key: 'p:a|user_a' } };

  it('notifies for any message while the tab is hidden', () => {
    expect(shouldNotifyFor(true, 'p:a|user_a', msg)).toBe(true);
  });

  it('notifies for messages outside the open channel', () => {
    expect(shouldNotifyFor(false, 'g:room-1', msg)).toBe(true);
  });

  it('stays quiet for the channel on screen', () => {
    expect(shouldNotifyFor(false, 'p:a|user_a', msg)).toBe(false);
    expect(shouldNotifyFor(false, undefined, msg)).toBe(true);
  });
});
