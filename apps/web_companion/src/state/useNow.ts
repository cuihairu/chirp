import { useEffect, useState } from 'react';

/**
 * Ticking wall-clock for render-time freshness checks (presence TTL, mute
 * badges). Date.now() is impure and must not run during render, so the
 * value rides state: captured once on mount, then refreshed on an interval.
 */
export function useNow(intervalMs: number): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now()), intervalMs);
    return () => clearInterval(timer);
  }, [intervalMs]);
  return now;
}
