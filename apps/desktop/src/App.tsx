import { Alert, Snackbar } from '@mui/material';
import { useCallback, useEffect, useState } from 'react';
import { ErrorCode } from '@chirp/proto/common';
import { errorText } from '@chirp/protocol/errors';
import type { Services } from './api/services';
import { createServices } from './api/services';
import LoginPage from './ui/LoginPage';
import MainWindow from './ui/MainWindow';

const HOST_KEY = 'chirp.desktop.host';

/**
 * 桌面端根组件：登录页 ⇄ 主窗口。服务图（connections + stores + api）
 * 在登录成功时创建、退出时销毁；被顶号（KICK）回到登录页并提示。
 */
export default function App() {
  const [services, setServices] = useState<Services | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [kickedNotice, setKickedNotice] = useState(false);
  const defaultHost = localStorage.getItem(HOST_KEY) ?? '127.0.0.1:5201';

  // 被顶号：auth store 的 kicked 翻转时回到登录页。
  useEffect(() => {
    if (!services) return;
    return services.auth.subscribe(() => {
      if (services.auth.get().kicked) {
        teardown();
        setKickedNotice(true);
      }
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [services]);

  const teardown = useCallback((): void => {
    setServices((prev) => {
      prev?.client.disconnect();
      prev?.social?.disconnect();
      prev?.party?.disconnect();
      prev?.voice?.disconnect();
      prev?.api.stop();
      return null;
    });
  }, []);

  const handleLogin = async (host: string, userId: string): Promise<void> => {
    setBusy(true);
    setError(null);
    localStorage.setItem(HOST_KEY, host);
    const next = createServices({ host });
    try {
      const code = await next.api.login(userId);
      if (code !== ErrorCode.OK) {
        next.client.disconnect();
        next.social?.disconnect();
        next.party?.disconnect();
        next.voice?.disconnect();
        setError(errorText(code));
        return;
      }
      setServices(next);
    } catch (err) {
      next.client.disconnect();
      next.social?.disconnect();
      next.party?.disconnect();
      next.voice?.disconnect();
      setError(
        err instanceof Error
          ? `连接失败：${err.message}`
          : `连接失败：${String(err)}`,
      );
    } finally {
      setBusy(false);
    }
  };

  const handleSignOut = useCallback((): void => {
    teardown();
  }, [teardown]);

  return (
    <>
      {services ? (
        <MainWindow services={services} onSignOut={handleSignOut} />
      ) : (
        <LoginPage defaultHost={defaultHost} busy={busy} error={error} onSubmit={(h, u) => void handleLogin(h, u)} />
      )}
      <Snackbar
        open={kickedNotice}
        autoHideDuration={6000}
        onClose={() => setKickedNotice(false)}
        anchorOrigin={{ vertical: 'top', horizontal: 'center' }}
      >
        <Alert severity="warning" variant="filled">
          会话已在其他设备登录（同平台顶号），请重新登录
        </Alert>
      </Snackbar>
    </>
  );
}
