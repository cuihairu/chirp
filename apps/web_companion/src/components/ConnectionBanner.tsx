import { useEffect, useState } from 'react';
import { Alert, Button } from '@mui/material';
import { useNavigate } from 'react-router-dom';
import type { ConnStatus } from '../protocol/chirp_client';
import { patch, useStoreValue } from '../state/store';
import { useServices } from '../api/services';
import { zh } from '../i18n/zh';

/**
 * Global connection banner. Two modes:
 * - kicked: the server handed this session to another device; auto-reconnect
 *   is already stopped client-side, so the only way out is back to login.
 * - reconnecting: transient drop while the client retries in the background.
 */
export default function ConnectionBanner() {
  const { client, api, auth } = useServices();
  const navigate = useNavigate();
  const { loggedIn, kicked } = useStoreValue(auth);
  const [status, setStatus] = useState<ConnStatus>(client.status);

  useEffect(
    () =>
      client.onStatus((next) => {
        setStatus(next);
        if (next === 'kicked') patch(auth, { kicked: true });
      }),
    [client, auth],
  );

  if (kicked) {
    return (
      <Alert
        severity="error"
        sx={{ borderRadius: 0 }}
        action={
          <Button color="inherit" size="small" onClick={() => void handleBackToLogin()}>
            {zh.banner.backToLogin}
          </Button>
        }
      >
        {zh.banner.kicked}
      </Alert>
    );
  }
  if (loggedIn && (status === 'waiting-reconnect' || status === 'connecting')) {
    return (
      <Alert severity="warning" sx={{ borderRadius: 0 }}>
        {zh.banner.reconnecting}
      </Alert>
    );
  }
  return null;

  async function handleBackToLogin(): Promise<void> {
    await api.logout();
    navigate('/login', { replace: true });
  }
}
