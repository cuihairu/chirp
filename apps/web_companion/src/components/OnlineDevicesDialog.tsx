import { Badge, Button, Dialog, DialogActions, DialogContent, DialogTitle, List, ListItem, ListItemText, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { onlineDevicesOf } from '../state/online_devices_store';
import { zh } from '../i18n/zh';

/**
 * 多端在线（P0）：本账号当前其他在线端的最小只读视图。数据来自登录响应的
 * online_devices 初始清单与 DEVICES_PRESENCE_NOTIFY 变更事件（chat 平面，
 * 随主连接到达，无需独立服务面）。
 */
export default function OnlineDevicesDialog({
  open,
  onClose,
}: {
  open: boolean;
  onClose: () => void;
}) {
  const { onlineDevices } = useServices();
  const devices = onlineDevicesOf(useStoreValue(onlineDevices));

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
