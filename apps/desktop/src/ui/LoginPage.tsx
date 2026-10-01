import { Alert, Box, Button, Paper, Stack, TextField, Typography } from '@mui/material';
import { useState } from 'react';
import type { FormEvent } from 'react';

/**
 * 登录页（第一阶段）：scaffold 模式——用户 ID 即 token，经 App 平面边缘
 * （chirp_app_sdk_gateway WS）转发 chirp_app_auth 校验（--allow_scaffold_login）。
 * 服务器地址形如 `127.0.0.1:5201`（app_gateway WS 口）；social/party/voice
 * 三个实验平面按同主机约定端口自动推导，宕机各自静默降级。
 */
export default function LoginPage(props: {
  defaultHost: string;
  busy: boolean;
  error: string | null;
  onSubmit: (host: string, userId: string) => void;
}) {
  const [host, setHost] = useState(props.defaultHost);
  const [userId, setUserId] = useState('');
  const [localError, setLocalError] = useState<string | null>(null);

  const submit = (event: FormEvent): void => {
    event.preventDefault();
    const trimmedHost = host.trim();
    const trimmedUser = userId.trim();
    if (!trimmedHost) {
      setLocalError('请填写服务器地址');
      return;
    }
    if (!trimmedUser) {
      setLocalError('请填写用户 ID');
      return;
    }
    setLocalError(null);
    props.onSubmit(trimmedHost, trimmedUser);
  };

  const shown = localError ?? props.error;
  return (
    <Box
      sx={{
        minHeight: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}
    >
      <Paper elevation={6} sx={{ p: 4, width: 380 }}>
        <Typography variant="h5" fontWeight={700}>
          Chirp
        </Typography>
        <Typography variant="body2" color="text.secondary" sx={{ mt: 0.5 }}>
          登录后与游戏好友私聊、群聊；不在游戏里也不掉线
        </Typography>
        <Box component="form" onSubmit={submit} sx={{ mt: 3 }}>
          <Stack spacing={2}>
            <TextField
              label="服务器地址"
              placeholder="127.0.0.1:5201"
              value={host}
              onChange={(e) => setHost(e.target.value)}
              size="small"
              helperText="App 平面边缘网关（WS）"
            />
            <TextField
              label="用户 ID"
              placeholder="dev 模式下用户 ID 即登录凭据"
              value={userId}
              onChange={(e) => setUserId(e.target.value)}
              size="small"
              autoFocus
            />
            {shown ? <Alert severity="error">{shown}</Alert> : null}
            <Button type="submit" variant="contained" disabled={props.busy}>
              {props.busy ? '登录中…' : '登录'}
            </Button>
          </Stack>
        </Box>
      </Paper>
    </Box>
  );
}
