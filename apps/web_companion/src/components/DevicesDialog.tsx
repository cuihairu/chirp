import { useState } from 'react';
import {
  Badge,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  List,
  ListItem,
  ListItemText,
  Typography,
} from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { zh } from '../i18n/zh';

/**
 * Device management: the account's registered push targets as the server
 * last reported them, with this browser marked and removable like any other.
 * Read-only mirror + unregister — registration happens automatically at
 * login (device_api.registerSelf).
 */
export default function DevicesDialog({ open, onClose }: { open: boolean; onClose: () => void }) {
  const { deviceApi, devices, auth } = useServices();
  const selfId = useStoreValue(auth).deviceId;
  const { devices: list, selfRegistered, unavailable } = useStoreValue(devices);
  const [error, setError] = useState<string | null>(null);

  const remove = async (deviceId: string): Promise<void> => {
    if (!deviceApi) return;
    const entry = list.find((d) => d.deviceId === deviceId);
    if (!window.confirm(zh.device.unregisterConfirm(entry?.deviceName || deviceId))) return;
    const ok = await deviceApi.unregister(deviceId);
    if (!ok) setError(zh.device.unregisterFailed);
  };

  return (
    <Dialog open={open} onClose={onClose} maxWidth="xs" fullWidth>
      <DialogTitle>{zh.device.title}</DialogTitle>
      <DialogContent>
        {unavailable ? (
          <Typography variant="body2" color="text.secondary">
            {zh.device.unavailable}
          </Typography>
        ) : (
          <>
            {!selfRegistered && (
              <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>
                {zh.device.notRegistered}
              </Typography>
            )}
            <Typography variant="caption" color="text.secondary" display="block" sx={{ mb: 1 }}>
              {zh.device.pushNote}
            </Typography>
            {error !== null && (
              <Typography variant="caption" color="error" display="block">
                {error}
              </Typography>
            )}
            {list.length === 0 ? (
              <Typography variant="body2" color="text.secondary">
                {zh.device.empty}
              </Typography>
            ) : (
              <List dense data-testid="device-list">
                {list.map((device) => {
                  const self = device.deviceId === selfId;
                  return (
                    <ListItem
                      key={device.deviceId}
                      secondaryAction={
                        <Button
                          size="small"
                          color="error"
                          onClick={() => void remove(device.deviceId)}
                          data-testid={`device-remove-${device.deviceId}`}
                        >
                          {zh.device.unregister}
                        </Button>
                      }
                    >
                      <ListItemText
                        primary={
                          self ? `${device.deviceName} (${zh.device.thisDevice})` : device.deviceName
                        }
                        secondary={`${device.platform} · ${new Date(
                          device.registeredAt,
                        ).toLocaleString()}`}
                      />
                    </ListItem>
                  );
                })}
              </List>
            )}
          </>
        )}
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>{zh.chat.cancel}</Button>
      </DialogActions>
    </Dialog>
  );
}

/** Entry button for the device plane (hidden when it is not configured). */
export function DevicesButton({ onClick }: { onClick: () => void }) {
  const { devices } = useServices();
  const { selfRegistered } = useStoreValue(devices);
  return (
    <Badge
      variant="dot"
      color="warning"
      invisible={selfRegistered}
      data-testid="device-entry-badge"
    >
      <Button variant="text" fullWidth onClick={onClick}>
        {zh.device.title}
      </Button>
    </Badge>
  );
}
