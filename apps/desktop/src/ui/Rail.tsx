import AddIcon from '@mui/icons-material/Add';
import DevicesOtherIcon from '@mui/icons-material/DevicesOther';
import GroupsIcon from '@mui/icons-material/Groups';
import PartyModeIcon from '@mui/icons-material/PartyMode';
import PersonAddAltIcon from '@mui/icons-material/PersonAddAlt';
import CampaignIcon from '@mui/icons-material/Campaign';
import {
  Badge,
  Box,
  Button,
  Chip,
  Divider,
  List,
  ListItem,
  ListItemButton,
  ListItemText,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { useState } from 'react';
import type { ReactNode } from 'react';
import type { Conversation } from '../state/models';
import { useStoreValue, type Store } from '../state/store';
import type { FriendState } from '../state/friend_store';
import type { OnlineDevicesState } from '../state/online_devices_store';
import type { PartyState } from '../state/party_store';
import type { PresenceState } from '../state/presence_store';

function SectionTitle(props: { children: string; action?: ReactNode }) {
  return (
    <Stack
      direction="row"
      alignItems="center"
      justifyContent="space-between"
      sx={{ px: 2, pt: 2, pb: 0.5 }}
    >
      <Typography variant="overline" color="text.secondary">
        {props.children}
      </Typography>
      {props.action}
    </Stack>
  );
}

function ConversationRow(props: {
  conversation: Conversation;
  active: boolean;
  onOpen: () => void;
}) {
  const { conversation } = props;
  return (
    <ListItemButton
      selected={props.active}
      onClick={props.onOpen}
      sx={{ borderRadius: 1, mx: 1 }}
    >
      <ListItemText
        primary={
          <Stack direction="row" alignItems="center" spacing={1}>
            {conversation.kind === 'group' ? (
              <GroupsIcon fontSize="small" color="secondary" />
            ) : null}
            <Typography variant="body2" noWrap sx={{ flex: 1 }}>
              {conversation.title}
            </Typography>
            {conversation.unreadLocal > 0 ? (
              <Badge
                color="primary"
                badgeContent={conversation.unreadLocal}
                max={99}
                sx={{ '& .MuiBadge-badge': { position: 'static', transform: 'none' } }}
              />
            ) : null}
          </Stack>
        }
        secondary={
          conversation.lastMessagePreview ? (
            <Typography variant="caption" color="text.secondary" noWrap>
              {conversation.lastMessagePreview}
            </Typography>
          ) : null
        }
      />
    </ListItemButton>
  );
}

/**
 * 左栏：会话（私聊+群聊）+ 好友 + 多端在线 + 功能入口（组队/语音，
 * 实验平面，断线静默降级）。
 */
export default function Rail(props: {
  conversations: Store<{ conversations: Conversation[] }>;
  friends: Store<FriendState>;
  presence: Store<PresenceState>;
  partyState: Store<PartyState>;
  onlineDevices: Store<OnlineDevicesState>;
  selfId: string;
  activeKey: string | null;
  planeStatus: { social: boolean; party: boolean; voice: boolean };
  onOpenConversation: (key: string) => void;
  onStartPrivate: (userId: string) => void;
  onAcceptFriend: (requestId: string) => void;
  onAddFriend: (userId: string) => void;
  onOpenGroupDialog: () => void;
  onOpenPartyDialog: () => void;
  onOpenVoiceDialog: () => void;
  onOpenDevicesDialog: () => void;
}) {
  const conversations = useStoreValue(props.conversations);
  const friends = useStoreValue(props.friends);
  const presence = useStoreValue(props.presence);
  const partyState = useStoreValue(props.partyState);
  const onlineDevices = useStoreValue(props.onlineDevices);
  const [newFriend, setNewFriend] = useState('');

  const onlineCount = Object.values(onlineDevices.byPlatform).filter((d) => d.online).length;

  return (
    <Box
      sx={{
        width: 300,
        flexShrink: 0,
        borderRight: '1px solid',
        borderColor: 'divider',
        bgcolor: 'background.paper',
        overflowY: 'auto',
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
      }}
    >
      <SectionTitle
        action={
          <Button size="small" startIcon={<AddIcon />} onClick={props.onOpenGroupDialog}>
            建群
          </Button>
        }
      >
        会话
      </SectionTitle>
      <List dense sx={{ pb: 0 }}>
        {conversations.conversations.map((c) => (
          <ConversationRow
            key={c.key}
            conversation={c}
            active={props.activeKey === c.key}
            onOpen={() => props.onOpenConversation(c.key)}
          />
        ))}
        {conversations.conversations.length === 0 ? (
          <Typography variant="caption" color="text.secondary" sx={{ px: 2, py: 1, display: 'block' }}>
            还没有会话——加个好友开聊，或建个群
          </Typography>
        ) : null}
      </List>

      <Divider sx={{ mt: 1 }} />
      <SectionTitle>好友请求</SectionTitle>
      {friends.pendingIn.length === 0 ? (
        <Typography variant="caption" color="text.secondary" sx={{ px: 2, display: 'block' }}>
          无
        </Typography>
      ) : (
        friends.pendingIn.map((req) => (
          <ListItem
            key={req.requestId}
            secondaryAction={
              <Button size="small" onClick={() => props.onAcceptFriend(req.requestId)}>
                接受
              </Button>
            }
          >
            <ListItemText primary={req.fromUserId} secondary="请求加你为好友" />
          </ListItem>
        ))
      )}

      <SectionTitle>好友</SectionTitle>
      <List dense>
        {friends.friends.map((uid) => {
          const p = presence.byUser[uid];
          const online = p ? p.status !== 0 : false; // PresenceStatus 0 = OFFLINE
          return (
            <ListItemButton
              key={uid}
              sx={{ borderRadius: 1, mx: 1 }}
              onClick={() => props.onStartPrivate(uid)}
            >
              <Chip
                size="small"
                sx={{ width: 8, height: 8, mr: 1, p: 0 }}
                color={online ? 'success' : 'default'}
                variant={online ? 'filled' : 'outlined'}
              />
              <ListItemText primary={uid} secondary={online ? '在线' : '离线'} />
            </ListItemButton>
          );
        })}
        {friends.friends.length === 0 ? (
          <Typography variant="caption" color="text.secondary" sx={{ px: 2, display: 'block' }}>
            {props.planeStatus.social ? '还没有好友' : 'social 平面未连接，好友面暂不可用'}
          </Typography>
        ) : null}
      </List>
      <Stack direction="row" spacing={1} sx={{ px: 2, pt: 0.5 }}>
        <TextField
          size="small"
          placeholder="用户 ID"
          value={newFriend}
          onChange={(e) => setNewFriend(e.target.value)}
          sx={{ flex: 1, '& .MuiInputBase-input': { py: 0.5 } }}
        />
        <Button
          size="small"
          startIcon={<PersonAddAltIcon />}
          disabled={!newFriend.trim() || !props.planeStatus.social}
          onClick={() => {
            props.onAddFriend(newFriend.trim());
            setNewFriend('');
          }}
        >
          加好友
        </Button>
      </Stack>

      <Divider sx={{ mt: 2 }} />
      <SectionTitle>入口</SectionTitle>
      <List dense>
        <ListItemButton sx={{ borderRadius: 1, mx: 1 }} onClick={props.onOpenPartyDialog}>
          <PartyModeIcon fontSize="small" sx={{ mr: 1.5 }} color="secondary" />
          <ListItemText
            primary="组队"
            secondary={
              props.planeStatus.party
                ? partyState.party
                  ? `队伍中 · ${partyState.party.members.length} 人`
                  : '跨游戏组队信令（实验）'
                : 'party 平面未连接'
            }
          />
        </ListItemButton>
        <ListItemButton sx={{ borderRadius: 1, mx: 1 }} onClick={props.onOpenVoiceDialog}>
          <CampaignIcon fontSize="small" sx={{ mr: 1.5 }} color="secondary" />
          <ListItemText
            primary="语音房"
            secondary={props.planeStatus.voice ? '语音信令（实验；媒体面未验证）' : 'voice 平面未连接'}
          />
        </ListItemButton>
        <ListItemButton sx={{ borderRadius: 1, mx: 1 }} onClick={props.onOpenDevicesDialog}>
          <DevicesOtherIcon fontSize="small" sx={{ mr: 1.5 }} />
          <ListItemText
            primary="我的在线设备"
            secondary={`本账号其他在线端 ${onlineCount} 个`}
          />
        </ListItemButton>
      </List>
      <Box sx={{ flex: 1 }} />
      <Typography variant="caption" color="text.disabled" sx={{ px: 2, py: 1.5 }}>
        Chirp 桌面版 · 会话经 App 网关（gateway+auth+chat）
      </Typography>
    </Box>
  );
}
