import { useState } from 'react';
import type { FormEvent } from 'react';
import { useNavigate } from 'react-router-dom';
import { Alert, Box, Button, Paper, TextField, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { errorText } from '../protocol/errors';
import { zh } from '../i18n/zh';

/**
 * Phase-one login: the user id IS the token (dev scaffold mode). Production
 * swaps in JWT via chat --token_secret without touching this flow.
 */
export default function LoginPage() {
  const { api } = useServices();
  const navigate = useNavigate();
  const [userId, setUserId] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const onSubmit = async (event: FormEvent): Promise<void> => {
    event.preventDefault();
    const id = userId.trim();
    if (!id || busy) return;
    setBusy(true);
    setError(null);
    try {
      const code = await api.login(id);
      if (code === 0) {
        navigate('/chat', { replace: true });
      } else {
        setError(errorText(code));
      }
    } catch {
      setError('无法连接到服务器,请稍后再试');
    } finally {
      setBusy(false);
    }
  };

  return (
    <Box
      sx={{
        minHeight: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        p: 2,
      }}
    >
      <Paper sx={{ p: 4, width: '100%', maxWidth: 360 }}>
        <Typography variant="h5" component="h1" gutterBottom>
          {zh.login.title}
        </Typography>
        <Typography variant="body2" color="text.secondary" sx={{ mb: 3 }}>
          {zh.login.subtitle}
        </Typography>
        <form onSubmit={(e) => void onSubmit(e)}>
          <TextField
            label={zh.login.userIdLabel}
            value={userId}
            onChange={(e) => setUserId(e.target.value)}
            fullWidth
            autoFocus
            margin="normal"
            error={error !== null}
            disabled={busy}
          />
          {error !== null && (
            <Alert severity="error" sx={{ mt: 1 }}>
              {error}
            </Alert>
          )}
          <Button
            type="submit"
            variant="contained"
            fullWidth
            sx={{ mt: 2 }}
            disabled={busy || userId.trim() === ''}
          >
            {busy ? zh.login.submitting : zh.login.submit}
          </Button>
        </form>
      </Paper>
    </Box>
  );
}
