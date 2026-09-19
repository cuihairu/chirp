/**
 * Short human-readable summary of a browser user agent for the device list
 * ("Chrome · Windows"). Best-effort: unknown inputs degrade to the raw
 * string's head, never throw.
 */
export function uaSummary(ua: string): string {
  const browser =
    /Edg\//.test(ua)
      ? 'Edge'
      : /OPR\//.test(ua)
        ? 'Opera'
        : /Firefox\//.test(ua)
          ? 'Firefox'
          : /Chrome\//.test(ua)
            ? 'Chrome'
            : /Safari\//.test(ua)
              ? 'Safari'
              : '';
  const os = /Windows/.test(ua)
    ? 'Windows'
    : /Android/.test(ua)
      ? 'Android'
      : /iPhone|iPad|iOS/.test(ua)
        ? 'iOS'
        : /Mac OS X/.test(ua)
          ? 'macOS'
          : /Linux/.test(ua)
            ? 'Linux'
            : '';
  const summary = [browser, os].filter(Boolean).join(' · ');
  return summary || 'Web';
}
