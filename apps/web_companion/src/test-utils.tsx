import type { ReactElement } from 'react';
import { render } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { MsgID } from '@chirp/proto/gateway';
import { createServices, ServicesProvider, type Services } from './api/services';
import type { ChatConnection } from './api/chat_api';
import { FakeChatConnection } from './state/test_helpers';

export interface MountedServices {
  services: Services;
  conn: FakeChatConnection;
}

/**
 * Render inside the real provider graph with a fake connection, already
 * logged in as user_a. `prepare` runs after login, before render — use it to
 * seed stores or override per-msgId responses.
 */
export async function renderLoggedIn(
  ui: ReactElement,
  options: {
    prepare?: (mounted: MountedServices) => void | Promise<void>;
    responder?: (msgId: MsgID, req: unknown) => Promise<unknown>;
    socialConn?: ChatConnection;
    partyConn?: ChatConnection;
  } = {},
): Promise<MountedServices> {
  const conn = new FakeChatConnection();
  const services = createServices({
    conn,
    socialConn: options.socialConn,
    partyConn: options.partyConn,
  });
  conn.setResponder(async (msgId, req) => options.responder?.(msgId, req) ?? { code: 0 });
  await services.api.login('user_a');
  const mounted = { services, conn };
  await options.prepare?.(mounted);
  render(
    <ServicesProvider value={services}>
      <MemoryRouter>{ui}</MemoryRouter>
    </ServicesProvider>,
  );
  return mounted;
}
