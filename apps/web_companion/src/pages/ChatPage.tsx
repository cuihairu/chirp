import { useCallback } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { Box, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import ChatWindow from '../components/ChatWindow';
import ConversationList from '../components/ConversationList';
import { zh } from '../i18n/zh';

/** Two-pane chat shell: conversation list on the left, open channel right. */
export default function ChatPage() {
  const { api, auth } = useServices();
  const navigate = useNavigate();
  const { channelKey } = useParams();
  const { userId } = useStoreValue(auth);

  const openChannel = useCallback(
    (key: string) => navigate(`/chat/${encodeURIComponent(key)}`),
    [navigate],
  );

  const signOut = async (): Promise<void> => {
    await api.logout();
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
