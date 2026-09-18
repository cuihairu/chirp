import { useMemo } from 'react';
import type { ReactNode } from 'react';
import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom';
import ConnectionBanner from './components/ConnectionBanner';
import ChatPage from './pages/ChatPage';
import LoginPage from './pages/LoginPage';
import { createServices, ServicesProvider, useServices, type Services } from './api/services';
import { useStoreValue } from './state/store';

function RequireAuth({ children }: { children: ReactNode }) {
  const { auth } = useServices();
  const { loggedIn } = useStoreValue(auth);
  // A page reload also lands here (stores are in-memory); re-login is cheap
  // because the device id persists per browser.
  if (!loggedIn) return <Navigate to="/login" replace />;
  return <>{children}</>;
}

function LoginRoute() {
  const { auth } = useServices();
  const { loggedIn } = useStoreValue(auth);
  if (loggedIn) return <Navigate to="/chat" replace />;
  return <LoginPage />;
}

export default function App({ services }: { services?: Services } = {}) {
  const resolved = useMemo(() => services ?? createServices(), [services]);
  return (
    <ServicesProvider value={resolved}>
      <BrowserRouter>
        <ConnectionBanner />
        <Routes>
          <Route path="/login" element={<LoginRoute />} />
          <Route
            path="/chat/:channelKey?"
            element={
              <RequireAuth>
                <ChatPage />
              </RequireAuth>
            }
          />
          <Route path="*" element={<Navigate to="/login" replace />} />
        </Routes>
      </BrowserRouter>
    </ServicesProvider>
  );
}
