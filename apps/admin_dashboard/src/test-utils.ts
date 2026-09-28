import { vi } from 'vitest';

export function jsonResponse(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json' },
  });
}

// Stub global fetch with a URL-routing fake. A synchronous throw from the
// handler becomes a rejected promise (network-failure simulations). Returns
// the vi.fn so tests can assert call counts/URLs. Pair with
// vi.unstubAllGlobals() in afterEach.
export function stubFetch(
  handler: (url: string) => Response | Promise<Response>,
): ReturnType<typeof vi.fn> {
  const fetchMock = vi.fn((input: RequestInfo | URL): Promise<Response> => {
    const url =
      typeof input === 'string'
        ? input
        : input instanceof URL
          ? input.toString()
          : input.url;
    try {
      return Promise.resolve(handler(url));
    } catch (error) {
      return Promise.reject(error);
    }
  });
  vi.stubGlobal('fetch', fetchMock);
  return fetchMock;
}
