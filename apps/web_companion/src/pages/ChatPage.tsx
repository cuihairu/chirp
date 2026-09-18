import { Box, Button, Typography } from '@mui/material';
import { useNavigate } from 'react-router-dom';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { zh } from '../i18n/zh';

/** Shell for the chat screen; the real layout lands with C7. */
export default function ChatPage() {
  const { api, auth } = useServices();
  const navigate = useNavigate();
  const { userId } = useStoreValue(auth);

  const signOut = async (): Promise<void> => {
    await api.logout();
    navigate('/login', { replace: true });
  };

  return (
    <Box sx={{ p: 3 }}>
      <Typography variant="h6">{zh.appName}</Typography>
      <Typography sx={{ mt: 2 }}>{zh.chat.welcome(userId ?? '')}</Typography>
      <Typography color="text.secondary">{zh.chat.placeholder}</Typography>
      <Button variant="outlined" sx={{ mt: 3 }} onClick={() => void signOut()}>
        {zh.chat.signOut}
      </Button>
    </Box>
  );
}
