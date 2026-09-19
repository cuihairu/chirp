import { useCallback, useEffect } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { Box, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { PresenceStatus } from '@chirp/proto/social';
import ChatWindow from '../components/ChatWindow';
import ConversationList from '../components/ConversationList';
import { zh } from '../i18n/zh';

/** Two-pane chat shell: conversation list on the left, open channel right. */
export default function ChatPage() {
  const { api, socialApi, partyApi, auth, social, party } = useServices();
  const navigate = useNavigate();
  const { channelKey } = useParams();
  const { userId } = useStoreValue(auth);

  // Social plane login is best-effort: when the service is down the chat
  // keeps working and friend features degrade (the api flags it internally).
  useEffect(() => {
    const id = auth.get().userId;
    if (!socialApi || !id) return;
    let cancelled = false;
    void socialApi.login(id).then((ok) => {
      if (!cancelled && ok) {
        void socialApi.setPresence(PresenceStatus.ONLINE);
      }
    });
    return () => {
      cancelled = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [socialApi]);

  // Party plane login, same degradeable contract as social.
  useEffect(() => {
    const id = auth.get().userId;
    if (!partyApi || !id) return;
    void partyApi.login(id).catch(() => {
      // Party unreachable: chat keeps working, party features hide.
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [partyApi]);

  const openChannel = useCallback(
    (key: string) => navigate(`/chat/${encodeURIComponent(key)}`),
    [navigate],
  );

  const signOut = async (): Promise<void> => {
    await api.logout();
    socialApi?.logout();
    partyApi?.logout();
    void social?.disconnect();
    void party?.disconnect();
    navigate('/login', { replace: true });
  };

  return (
    <Box sx={{ display: 'flex', height: '100vh', overflow: 'hidden' }}>
      <Box sx={{ width: 260, flexShrink: 0, display: 'flex', flexDirection: 'column' }}>
        <Box sx={{ px: 2, py: 1.5, display: 'flex', alignItems: 'baseline', gap: 1 }}>
          <Typography variant="subtitle1">{zh.appName}</Typography>
          <Typography variant="caption" color="text.secondary">
            {userId}
          </Typography>
        </Box>
        <ConversationList activeKey={channelKey} onOpen={openChannel} />
        <Box sx={{ p: 1.5 }}>
          <Typography
            variant="caption"
            color="text.secondary"
            sx={{ cursor: 'pointer', textDecoration: 'underline' }}
            onClick={() => void signOut()}
          >
            {zh.chat.signOut}
          </Typography>
        </Box>
      </Box>
      {channelKey ? (
        <ChatWindow key={channelKey} channelKey={channelKey} />
      ) : (
        <Box sx={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <Typography color="text.secondary">{zh.chat.emptyChannel}</Typography>
        </Box>
      )}
    </Box>
  );
}
