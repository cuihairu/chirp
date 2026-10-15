import { describe, expect, it } from 'vitest';
import { GROUP_TTL_OPTIONS, groupExpiryLabel } from './Rail';

/** 临时频道剩余时长文案：分/时/天三档，到点返回 null（名册修剪接手）。 */
describe('groupExpiryLabel', () => {
  const now = 1_700_000_000_000;

  it('returns null at or past expiry', () => {
    expect(groupExpiryLabel(now, now)).toBeNull();
    expect(groupExpiryLabel(now - 1, now)).toBeNull();
  });

  it('rounds up to minutes under an hour', () => {
    expect(groupExpiryLabel(now + 30_000, now)).toBe('剩 1 分');
    expect(groupExpiryLabel(now + 59 * 60_000, now)).toBe('剩 59 分');
  });

  it('switches to hours under 48h and days beyond', () => {
    expect(groupExpiryLabel(now + 60 * 60_000, now)).toBe('剩 1 小时');
    expect(groupExpiryLabel(now + 47 * 60 * 60_000, now)).toBe('剩 47 小时');
    expect(groupExpiryLabel(now + 48 * 60 * 60_000, now)).toBe('剩 2 天');
  });

  it('offers a permanent tier (0) first', () => {
    expect(GROUP_TTL_OPTIONS[0]).toEqual([0, '永久']);
  });
});
