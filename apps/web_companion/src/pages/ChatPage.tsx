import { useCallback, useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { Box, Typography } from '@mui/material';
import { useServices } from '../api/services';
import { useStoreValue } from '../state/store';
import { PresenceStatus } from '@chirp/proto/social';
import { shouldNotifyFor } from '../api/desktop_notify';
import ChatWindow from '../components/ChatWindow';
import ConversationList from '../components/ConversationList';
import { zh } from '../i18n/zh';

/** Two-pane chat shell: conversation list on the left, open channel right. */
export default function ChatPage() {
  const { api, socialApi, partyApi, deviceApi, auth, social, party, device, conversations } =
    useServices();
  const navigate = useNavigate();
  const { channelKey } = useParams();
  const { userId } = useStoreValue(auth);
  const [notifyPerm, setNotifyPerm] = useState<string>(
    typeof Notification !== 'undefined' ? Notification.permission : 'denied',
  );

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

  // Device plane login, same degradeable contract; a successful login also
  // registers this browser as a push target (registerSelf inside login).
  useEffect(() => {
    const id = auth.get().userId;
    if (!deviceApi || !id) return;
    void deviceApi.login(id).catch(() => {
      // Edge unreachable: chat keeps working, device features hide.
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [deviceApi]);

  const openChannel = useCallback(
    (key: string) => navigate(`/chat/${encodeURIComponent(key)}`),
    [navigate],
  );

  // Desktop notifications: fire for live messages that are not on screen.
  // The permission is never requested automatically — the header button does.
  useEffect(() => {
    if (!api || notifyPerm !== 'granted') return;
    return api.onMessage((message) => {
      if (!shouldNotifyFor(document.hidden, channelKey, message)) return;
      const groupTitle = conversations
        .get()
        .conversations.find((c) => c.key === message.channel.key)?.title;
      const title =
        message.channel.kind === 'private'
          ? zh.notify.privateTitle(message.fromUserId)
          : zh.notify.groupTitle(groupTitle ?? message.channel.peerId, message.fromUserId);
      const notification = new Notification(title, {
        body: message.content,
        tag: message.channel.key,
      });
      notification.onclick = () => {
        window.focus();
        openChannel(message.channel.key);
        notification.close();
      };
    });
  }, [api, notifyPerm, channelKey, openChannel, conversations]);

  const requestNotify = (): void => {
    if (typeof Notification === 'undefined') return;
    void Notification.requestPermission().then((perm) => setNotifyPerm(perm));
  };

  const signOut = async (): Promise<void> => {
    await api.logout();
    socialApi?.logout();
    partyApi?.logout();
    deviceApi?.logout();
    void social?.disconnect();
    void party?.disconnect();
    void device?.disconnect();
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
        <Box sx={{ p: 1.5, display: 'flex', flexDirection: 'column', gap: 0.5 }}>
          {notifyPerm === 'default' && (
            <Typography
              variant="caption"
              color="text.secondary"
              sx={{ cursor: 'pointer', textDecoration: 'underline' }}
              onClick={requestNotify}
              data-testid="notify-enable"
            >
              {zh.notify.enable}
            </Typography>
          )}
          {notifyPerm === 'granted' && (
            <Typography variant="caption" color="text.secondary">
              {zh.notify.enabled}
            </Typography>
          )}
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
