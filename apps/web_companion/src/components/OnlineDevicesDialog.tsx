import { useEffect, useState } from 'react';
import { Badge, Box, Button, Dialog, DialogActions, DialogContent, DialogTitle, Divider, FormControlLabel, List, ListItem, ListItemText, Switch, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { onlineDevicesOf } from '../state/online_devices_store';
import { zh } from '../i18n/zh';

/**
 * 多端在线（P0）：本账号当前其他在线端的最小只读视图。数据来自登录响应的
 * online_devices 初始清单与 DEVICES_PRESENCE_NOTIFY 变更事件（chat 平面，
 * 随主连接到达，无需独立服务面）。
 *
 * 附带「游戏在线状态」开关（游戏在线状态任务）：走 app_gateway 同一 socket,
 * 关闭后好友既看不到「正在玩 X」,好友私聊也不再投递进游戏。
 */
export default function OnlineDevicesDialog({
  open,
  onClose,
}: {
  open: boolean;
  onClose: () => void;
}) {
  const { onlineDevices, gamePresence, gamePresenceApi } = useServices();
  const devices = onlineDevicesOf(useStoreValue(onlineDevices));
  const presence = useStoreValue(gamePresence);
  const [failed, setFailed] = useState(false);

  // 打开即取权威快照:开关默认值(未设置过 → true)与生效游戏清单只有服务端知道。
  useEffect(() => {
    if (open && gamePresenceApi) {
      setFailed(false);
      void gamePresenceApi.refresh();
    }
  }, [open, gamePresenceApi]);

  const toggle = async (enabled: boolean): Promise<void> => {
    if (!gamePresenceApi) return;
    setFailed(!(await gamePresenceApi.setEnabled(enabled)));
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>{zh.onlineDevices.title}</DialogTitle>
      <DialogContent>
        <Typography variant="caption" color="text.secondary" display="block" sx={{ mb: 1 }}>
          {zh.onlineDevices.note}
        </Typography>
        {devices.length === 0 ? (
          <Typography variant="body2" color="text.secondary" data-testid="online-devices-empty">
            {zh.onlineDevices.empty}
          </Typography>
        ) : (
          <List dense data-testid="online-devices-list">
            {devices.map((device) => (
              <ListItem key={device.platform} data-testid={`online-device-${device.platform}`}>
                <ListItemText
                  primary={`${device.platform} · ${device.deviceId}`}
                  secondary={
                    device.online
                      ? zh.onlineDevices.online
                      : `${zh.onlineDevices.offline} · ${new Date(device.at).toLocaleTimeString()}`
                  }
                />
                <Badge
                  variant="dot"
                  color="success"
                  invisible={!device.online}
                  sx={{ mr: 1 }}
                  data-testid={`online-device-dot-${device.platform}`}
                />
              </ListItem>
            ))}
          </List>
        )}
        {gamePresenceApi && (
          <Box sx={{ mt: 1 }} data-testid="game-presence-section">
            <Divider sx={{ mb: 1 }} />
            <FormControlLabel
              data-testid="game-presence-toggle"
              control={
                <Switch
                  checked={presence.enabled}
                  disabled={!presence.loaded}
                  onChange={(e) => void toggle(e.target.checked)}
                />
              }
              label={zh.gamePresence.title}
            />
            <Typography variant="caption" color="text.secondary" display="block">
              {zh.gamePresence.note}
            </Typography>
            {presence.loaded && (
              <Typography variant="caption" color="text.secondary" display="block" data-testid="game-presence-state">
                {!presence.enabled
                  ? zh.gamePresence.off
                  : presence.games.length > 0
                    ? zh.gamePresence.games(presence.games)
                    : zh.gamePresence.noGames}
              </Typography>
            )}
            {failed && (
              <Typography variant="caption" color="error" display="block" data-testid="game-presence-failed">
                {zh.gamePresence.failed}
              </Typography>
            )}
          </Box>
        )}
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}

/** Sidebar entry; shows the live count when any other device is online. */
export function OnlineDevicesButton({ onClick }: { onClick: () => void }) {
  const { onlineDevices } = useServices();
  const live = onlineDevicesOf(useStoreValue(onlineDevices)).filter((d) => d.online).length;
  return (
    <Badge
      badgeContent={live > 0 ? live : undefined}
      color="success"
      data-testid="online-devices-entry-badge"
    >
      <Button variant="text" fullWidth onClick={onClick}>
        {zh.onlineDevices.title}
      </Button>
    </Badge>
  );
}
