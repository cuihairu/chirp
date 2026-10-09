import LogoutIcon from '@mui/icons-material/Logout';
import {
  Box,
  Button,
  Stack,
  Toolbar,
  Tooltip,
  Typography,
} from '@mui/material';
import { useEffect, useMemo, useState } from 'react';
import type { ChannelRef } from '../api/chat_api';
import { conversationOf, groupKey, privateKey, type Conversation } from '../state/models';
import { clearUnread, upsertConversation } from '../state/conversation_store';
import { useStoreValue } from '../state/store';
import type { Services } from '../api/services';
import ChatPanel from './ChatPanel';
import Rail from './Rail';
import SearchDialog from './SearchDialog';
import {
  CreateGroupDialog,
  DevicesDialog,
  GroupManageDialog,
  PartyDialog,
  VoiceDialog,
} from './Dialogs';
import { ensureNotifyPermission, fireNotify, shouldNotifyFor } from './notify';

/**
 * 主窗口：左栏（会话/好友/入口）+ 右侧聊天面板 + 功能对话框。登录后
 * social/party/voice 三个实验平面各自尽力登录（宕机静默降级，与
 * web_companion 同一契约）。
 */
export default function MainWindow(props: {
  services: Services;
  onSignOut: () => void;
}) {
  const { services, onSignOut } = props;
  const { api, socialApi, partyApi, voiceApi } = services;
  const authState = useStoreValue(services.auth);
  const conversations = useStoreValue(services.conversations);
  const selfId = authState.userId ?? '';
  const [activeKey, setActiveKey] = useState<string | null>(null);
  const [groupDialogOpen, setGroupDialogOpen] = useState(false);
  const [manageGroup, setManageGroup] = useState<{ id: string; name: string } | null>(null);
  const [partyOpen, setPartyOpen] = useState(false);
  const [voiceOpen, setVoiceOpen] = useState(false);
  const [devicesOpen, setDevicesOpen] = useState(false);
  const [searchOpen, setSearchOpen] = useState(false);
  const [planeUp, setPlaneUp] = useState({ social: false, party: false, voice: false });
  const friends = useStoreValue(services.friends);

  // presence 只向好友广播：登录后才结识的好友错过此前的广播，名单一变
  // 就主动拉一次批量在线状态补齐。
  useEffect(() => {
    if (!socialApi || friends.friends.length === 0) return;
    void socialApi.pullPresence(friends.friends);
  }, [socialApi, friends.friends]);

  // 登录引导：拉一次群名单。会话列表的群入口不能只靠建群/受邀 notify——
  // 重登后 notify 都错过了，不主动拉就丢群。
  useEffect(() => {
    void api.refreshGroups();
  }, [api]);

  // 实验平面登录：各平面独立降级（unreachable → 隐藏对应功能面）。
  useEffect(() => {
    const userId = services.auth.get().userId;
    if (!userId) return;
    if (socialApi) {
      socialApi
        .login(userId)
        .then((ok) => setPlaneUp((prev) => ({ ...prev, social: ok })))
        .catch(() => setPlaneUp((prev) => ({ ...prev, social: false })));
    }
    if (partyApi) {
      partyApi
        .login(userId)
        .then((ok) => setPlaneUp((prev) => ({ ...prev, party: ok })))
        .catch(() => setPlaneUp((prev) => ({ ...prev, party: false })));
    }
    if (voiceApi) {
      voiceApi
        .login(userId)
        .then((ok) => setPlaneUp((prev) => ({ ...prev, voice: ok })))
        .catch(() => setPlaneUp((prev) => ({ ...prev, voice: false })));
    }
  }, [socialApi, partyApi, voiceApi]);

  // 桌面通知：用户点一次授权（不自动弹）；之后对「不在屏幕上」的实时
  // 消息发系统通知。
  const [notifyOn, setNotifyOn] = useState(false);
  useEffect(() => {
    return api.onMessage((message) => {
      if (!notifyOn) return;
      if (!shouldNotifyFor(document.hidden, activeKey ?? undefined, message)) return;
      const title =
        message.channel.kind === 'private'
          ? `私聊 · ${message.fromUserId}`
          : `${conversationsOf(services).find((c) => c.key === message.channel.key)?.title ?? message.channel.peerId} · ${message.fromUserId}`;
      fireNotify(title, message.content, message.channel.key, () => setActiveKey(message.channel.key));
    });
  }, [api, notifyOn, activeKey, services]);

  const toggleNotify = async (): Promise<void> => {
    if (notifyOn) {
      setNotifyOn(false);
      return;
    }
    setNotifyOn(await ensureNotifyPermission());
  };

  const activeConversation = useMemo(
    () => conversations.conversations.find((c) => c.key === activeKey) ?? null,
    [conversations, activeKey],
  );

  const openConversation = (key: string): void => {
    clearUnread(services.conversations, key);
    setActiveKey(key);
  };

  // 点好友/输入用户 ID 开私聊：本地 upsert（服务端无私聊清单语义），
  // 历史在打开频道时从服务端分页拉。
  const startPrivate = (peer: string): void => {
    if (!peer || peer === selfId) return;
    upsertConversation(services.conversations, {
      kind: 'private',
      key: privateKey(selfId, peer),
      channelId: [selfId, peer].sort().join('|'),
      peerId: peer,
      title: peer,
      unreadLocal: 0,
    });
    clearUnread(services.conversations, privateKey(selfId, peer));
    setActiveKey(privateKey(selfId, peer));
  };

  const acceptFriend = (requestId: string): void => {
    void socialApi?.respondRequest(requestId, true);
  };

  const addFriend = (userId: string): void => {
    void socialApi?.addFriend(userId);
  };

  const channelOf = (conversation: Conversation): ChannelRef => {
    const { kind, channelId } = conversationOf(conversation.key);
    return {
      key: conversation.key,
      kind,
      channelId,
      peerId: conversation.peerId,
    };
  };

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', height: '100vh' }}>
      <Toolbar variant="dense" sx={{ bgcolor: 'background.paper', borderBottom: '1px solid', borderColor: 'divider' }}>
        <Typography variant="h6" fontWeight={700} sx={{ mr: 2 }}>
          Chirp
        </Typography>
        <Typography variant="body2" color="text.secondary">
          {selfId}
          {authState.kicked ? '（会话已被其他设备接管）' : ''}
        </Typography>
        <Box sx={{ flex: 1 }} />
        <Stack direction="row" spacing={1}>
          <Button size="small" onClick={() => setSearchOpen(true)} data-testid="search-open">
            搜索
          </Button>
          <Tooltip title={notifyOn ? '桌面通知已开启' : '开启桌面通知'}>
            <Button size="small" variant={notifyOn ? 'contained' : 'outlined'} onClick={() => void toggleNotify()}>
              通知
            </Button>
          </Tooltip>
          <Button size="small" startIcon={<LogoutIcon />} onClick={() => void api.logout().then(onSignOut)}>
            退出
          </Button>
        </Stack>
      </Toolbar>

      <Box sx={{ display: 'flex', flex: 1, minHeight: 0 }}>
        <Rail
          conversations={services.conversations}
          friends={services.friends}
          presence={services.presence}
          partyState={services.partyState}
          onlineDevices={services.onlineDevices}
          selfId={selfId}
          activeKey={activeKey}
          planeStatus={planeUp}
          onOpenConversation={openConversation}
          onStartPrivate={startPrivate}
          onAcceptFriend={acceptFriend}
          onAddFriend={addFriend}
          onOpenGroupDialog={() => setGroupDialogOpen(true)}
          onOpenPartyDialog={() => setPartyOpen(true)}
          onOpenVoiceDialog={() => setVoiceOpen(true)}
          onOpenDevicesDialog={() => setDevicesOpen(true)}
        />

        <Box sx={{ flex: 1, minWidth: 0 }}>
          {activeConversation ? (
            <Stack sx={{ height: '100%' }}>
              <Box sx={{ flex: 1, minHeight: 0 }}>
                <ChatPanel
                  api={api}
                  messages={services.messages}
                  typing={services.typing}
                  conversation={activeConversation}
                  selfId={selfId}
                  channel={channelOf(activeConversation)}
                />
              </Box>
              {activeConversation.kind === 'group' ? (
                <Button size="small" onClick={() => setManageGroup({ id: activeConversation.peerId, name: activeConversation.title })}>
                  群管理
                </Button>
              ) : null}
            </Stack>
          ) : (
            <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%' }}>
              <Typography color="text.secondary">从左侧选择一个会话开始聊天</Typography>
            </Box>
          )}
        </Box>
      </Box>

      <CreateGroupDialog
        api={api}
        open={groupDialogOpen}
        onClose={() => setGroupDialogOpen(false)}
        onCreated={(groupId) => setActiveKey(groupKey(groupId))}
      />
      {manageGroup ? (
        <GroupManageDialog
          api={api}
          groupId={manageGroup.id}
          groupName={manageGroup.name}
          selfId={selfId}
          open
          onClose={() => setManageGroup(null)}
          onLeft={() => setActiveKey(null)}
        />
      ) : null}
      <PartyDialog
        api={partyApi}
        state={services.partyState}
        selfId={selfId}
        open={partyOpen}
        onClose={() => setPartyOpen(false)}
      />
      <VoiceDialog
        api={voiceApi}
        state={services.voiceState}
        open={voiceOpen}
        onClose={() => setVoiceOpen(false)}
      />
      <DevicesDialog
        state={services.onlineDevices}
        selfId={selfId}
        open={devicesOpen}
        onClose={() => setDevicesOpen(false)}
      />
      <SearchDialog
        api={api}
        conversations={services.conversations}
        selfId={selfId}
        open={searchOpen}
        onClose={() => setSearchOpen(false)}
        onOpenChannel={(key) => {
          clearUnread(services.conversations, key);
          setActiveKey(key);
        }}
      />
    </Box>
  );
}

/** conversations store 的当前清单（通知标题用）。 */
function conversationsOf(services: Services): Conversation[] {
  return services.conversations.get().conversations;
}
