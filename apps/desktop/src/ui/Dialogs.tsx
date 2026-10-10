import DeleteIcon from '@mui/icons-material/Delete';
import {
  Alert,
  Box,
  Button,
  Chip,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  List,
  ListItem,
  ListItemText,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import { GroupMemberRole } from '@chirp/proto/chat';
import type { ChatApi } from '../api/chat_api';
import type { PartyApi } from '../api/party_api';
import type { VoiceApi } from '../api/voice_api';
import { useStoreValue, type Store } from '../state/store';
import type { OnlineDevicesState } from '../state/online_devices_store';
import type { PartyState } from '../state/party_store';
import type { VoiceState } from '../state/voice_store';

function Shell(props: {
  title: string;
  open: boolean;
  onClose: () => void;
  children: ReactNode;
  actions?: ReactNode;
  width?: number;
}) {
  return (
    <Dialog open={props.open} onClose={props.onClose} fullWidth maxWidth="xs">
      <DialogTitle>{props.title}</DialogTitle>
      <DialogContent dividers>{props.children}</DialogContent>
      <DialogActions>
        {props.actions}
        <Button onClick={props.onClose}>关闭</Button>
      </DialogActions>
    </Dialog>
  );
}

/** 建群：名称（+ 可选描述）→ 返回群 ID 并打开群会话。 */
export function CreateGroupDialog(props: {
  api: ChatApi;
  open: boolean;
  onClose: () => void;
  onCreated: (groupId: string) => void;
}) {
  const [name, setName] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const create = async (): Promise<void> => {
    if (!name.trim()) return;
    setBusy(true);
    setError(null);
    try {
      const groupId = await props.api.createGroup(name.trim());
      if (groupId) {
        setName('');
        props.onCreated(groupId);
        props.onClose();
      } else {
        setError('建群失败（服务端拒绝或断线）');
      }
    } catch {
      // 超时等异常会 reject（requestTimeoutMs），不复位 busy 会锁死按钮。
      setError('建群失败（服务端拒绝或断线）');
    } finally {
      setBusy(false);
    }
  };

  return (
    <Shell
      title="建群"
      open={props.open}
      onClose={props.onClose}
      actions={
        <Button onClick={() => void create()} disabled={busy || !name.trim()} variant="contained">
          创建
        </Button>
      }
    >
      <Stack spacing={2}>
        <TextField
          size="small"
          label="群名称"
          value={name}
          onChange={(e) => setName(e.target.value)}
          autoFocus
        />
        {error ? <Alert severity="error">{error}</Alert> : null}
      </Stack>
    </Shell>
  );
}

/** 群管理最小面：邀请 / 踢人 / 退群。成员清单由 loadGroupMembers 拉取。 */
export function GroupManageDialog(props: {
  api: ChatApi;
  groupId: string;
  groupName: string;
  selfId: string;
  open: boolean;
  onClose: () => void;
  onLeft: () => void;
}) {
  const [inviteId, setInviteId] = useState('');
  const [members, setMembers] = useState<Awaited<ReturnType<ChatApi['loadGroupMembers']>>>([]);
  const [error, setError] = useState<string | null>(null);

  const refresh = async (): Promise<void> => {
    try {
      const list = await props.api.loadGroupMembers(props.groupId);
      setMembers(list ?? []);
    } catch {
      setMembers([]);
    }
  };

  // 打开时拉一次成员清单（简单起见不做实时订阅）。
  useEffect(() => {
    void refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // 禁言入口按名册角色放行（MODERATOR 及以上）；名册未载入时不放行。
  const selfRole = members.find((m) => m.userId === props.selfId)?.role;
  const canMute =
    selfRole === GroupMemberRole.MODERATOR ||
    selfRole === GroupMemberRole.ADMIN ||
    selfRole === GroupMemberRole.OWNER;

  // 禁言时长走行内输入而非 window.prompt——Tauri(wry) WebView 不实现
  // prompt/confirm/alert，弹窗会静默返回 null，按钮看起来没反应。
  const [muteTarget, setMuteTarget] = useState<string | null>(null);
  const [muteMinutes, setMuteMinutes] = useState('10');

  // 群昵称(2122/2123)：行内输入而非 window.prompt（同禁言——Tauri(wry)
  // WebView 不实现 prompt）。本人随时可设；MODERATOR+ 可设他人。空串 = 清除。
  const [aliasTarget, setAliasTarget] = useState<string | null>(null);
  const [aliasText, setAliasText] = useState('');

  const confirmMute = async (): Promise<void> => {
    if (!muteTarget) return;
    const seconds = Math.max(0, Math.floor(Number(muteMinutes) * 60) || 0);
    if (seconds === 0) {
      setMuteTarget(null);
      return;
    }
    setError(null);
    try {
      const resp = await props.api.setGroupMute(props.groupId, muteTarget, seconds);
      if (resp.code !== 0) {
        setError(`禁言操作失败（code ${resp.code}）`);
        return;
      }
      setMuteTarget(null);
      void refresh();
    } catch {
      setError('禁言操作失败（服务端拒绝或断线）');
    }
  };

  /** 已禁言成员点击按钮直接解禁(seconds=0)；未禁言成员展开行内时长输入。 */
  const toggleMute = async (target: string, mutedUntilTs: number): Promise<void> => {
    if (mutedUntilTs > Date.now()) {
      setError(null);
      try {
        const resp = await props.api.setGroupMute(props.groupId, target, 0);
        if (resp.code !== 0) {
          setError(`禁言操作失败（code ${resp.code}）`);
          return;
        }
        void refresh();
      } catch {
        setError('禁言操作失败（服务端拒绝或断线）');
      }
      return;
    }
    setMuteMinutes('10');
    setMuteTarget(target);
  };

  const startAlias = (target: string, current: string): void => {
    setAliasText(current);
    setAliasTarget(target);
  };

  const confirmAlias = async (): Promise<void> => {
    if (!aliasTarget) return;
    setError(null);
    try {
      const resp = await props.api.setMemberAlias(props.groupId, aliasTarget, aliasText.trim());
      if (resp.code !== 0) {
        setError(`群昵称设置失败（code ${resp.code}）`);
        return;
      }
      setAliasTarget(null);
      void refresh();
    } catch {
      setError('群昵称设置失败（服务端拒绝或断线）');
    }
  };

  const invite = async (): Promise<void> => {
    setError(null);
    try {
      const code = await props.api.inviteToGroup(props.groupId, inviteId.trim());
      if (code !== 0) setError(`邀请失败（code ${code}）`);
      setInviteId('');
      void refresh();
    } catch {
      setError('邀请失败（服务端拒绝或断线）');
    }
  };

  const leave = async (): Promise<void> => {
    try {
      const code = await props.api.leaveGroup(props.groupId);
      if (code !== 0) {
        setError(`退群失败（code ${code}）`);
        return;
      }
      props.onLeft();
      props.onClose();
    } catch {
      setError('退群失败（服务端拒绝或断线）');
    }
  };

  return (
    <Shell
      title={`群管理 · ${props.groupName}`}
      open={props.open}
      onClose={props.onClose}
      actions={
        <Button color="error" onClick={() => void leave()}>
          退群
        </Button>
      }
    >
      <Stack spacing={2}>
        <Stack direction="row" spacing={1}>
          <TextField
            size="small"
            label="邀请用户 ID"
            value={inviteId}
            onChange={(e) => setInviteId(e.target.value)}
            sx={{ flex: 1 }}
          />
          <Button onClick={() => void invite()} disabled={!inviteId.trim()}>
            邀请
          </Button>
        </Stack>
        {error ? <Alert severity="error">{error}</Alert> : null}
        {muteTarget ? (
          <Stack direction="row" spacing={1} alignItems="center">
            <Typography variant="body2" sx={{ whiteSpace: 'nowrap' }}>
              禁言 {muteTarget}（分钟）
            </Typography>
            <TextField
              size="small"
              value={muteMinutes}
              onChange={(e) => setMuteMinutes(e.target.value)}
              sx={{ width: 90 }}
              autoFocus
              data-testid="mute-minutes"
            />
            <Button variant="contained" size="small" onClick={() => void confirmMute()} data-testid="mute-confirm">
              确认
            </Button>
            <Button size="small" onClick={() => setMuteTarget(null)}>
              取消
            </Button>
          </Stack>
        ) : null}
        {aliasTarget ? (
          <Stack direction="row" spacing={1} alignItems="center">
            <Typography variant="body2" sx={{ whiteSpace: 'nowrap' }}>
              群昵称 {aliasTarget}（留空清除）
            </Typography>
            <TextField
              size="small"
              value={aliasText}
              onChange={(e) => setAliasText(e.target.value)}
              sx={{ width: 140 }}
              autoFocus
              data-testid="alias-input"
            />
            <Button
              variant="contained"
              size="small"
              onClick={() => void confirmAlias()}
              data-testid="alias-confirm"
            >
              确认
            </Button>
            <Button size="small" onClick={() => setAliasTarget(null)}>
              取消
            </Button>
          </Stack>
        ) : null}
        <Box>
          <Typography variant="caption" color="text.secondary">
            成员（{members.length}）
          </Typography>
          <List dense>
            {members.map((m) => {
              const muted = m.mutedUntilTs > Date.now();
              return (
                <ListItem key={m.userId} secondaryAction={
                  <Stack direction="row" spacing={0.5} alignItems="center">
                    {m.userId === props.selfId ? <Chip size="small" label="我" /> : null}
                    {m.userId === props.selfId || canMute ? (
                      <Button
                        size="small"
                        onClick={() => startAlias(m.userId, m.alias)}
                        data-testid={`alias-${m.userId}`}
                      >
                        群昵称
                      </Button>
                    ) : null}
                    {m.userId !== props.selfId && canMute ? (
                      <Button
                        size="small"
                        onClick={() => void toggleMute(m.userId, m.mutedUntilTs)}
                        data-testid={`mute-${m.userId}`}
                      >
                        {muted ? '解除禁言' : '禁言'}
                      </Button>
                    ) : null}
                    {m.userId !== props.selfId ? (
                      <Button
                        size="small"
                        color="error"
                        startIcon={<DeleteIcon />}
                        onClick={async () => {
                          await props.api.kickMember(props.groupId, m.userId);
                          void refresh();
                        }}
                      >
                        踢出
                      </Button>
                    ) : null}
                  </Stack>
                }>
                  <ListItemText
                    primary={(m.alias ? `${m.alias}（${m.userId}）` : m.userId) + (muted ? '（禁言中）' : '')}
                    secondary={m.role === GroupMemberRole.OWNER ? '群主' : undefined}
                  />
                </ListItem>
              );
            })}
          </List>
        </Box>
      </Stack>
    </Shell>
  );
}

/** 组队面板（party 实验平面）：建队/邀请/就绪/离队/解散 + 受邀处理。 */
export function PartyDialog(props: {
  api: PartyApi | null;
  state: Store<PartyState>;
  selfId: string;
  open: boolean;
  onClose: () => void;
}) {
  const partyState = useStoreValue(props.state);
  const [target, setTarget] = useState('');
  const [notice, setNotice] = useState<string | null>(null);
  const party = partyState.party;

  const run = async (fn: () => Promise<number | boolean>, okText: string): Promise<void> => {
    setNotice(null);
    const code = await fn();
    setNotice(code === 0 || code === true ? okText : `party 服务返回 code ${code}`);
  };

  return (
    <Shell title="组队（跨游戏 · 实验平面）" open={props.open} onClose={props.onClose}>
      <Stack spacing={2}>
        {!props.api ? (
          <Alert severity="info">party 平面未连接（服务未启动或不可达）</Alert>
        ) : !party ? (
          <>
            {partyState.invites.length > 0 ? (
              <Box>
                <Typography variant="caption" color="text.secondary">
                  收到的邀请
                </Typography>
                <List dense>
                  {partyState.invites.map((inv) => (
                    <ListItem
                      key={inv.inviteId}
                      secondaryAction={
                        <Stack direction="row" spacing={0.5}>
                          <Button
                            size="small"
                            variant="contained"
                            onClick={() => void run(() => props.api!.acceptInvite(inv.inviteId), '已加入队伍')}
                          >
                            接受
                          </Button>
                          <Button
                            size="small"
                            onClick={() => void run(() => props.api!.declineInvite(inv.inviteId), '已拒绝')}
                          >
                            拒绝
                          </Button>
                        </Stack>
                      }
                    >
                      <ListItemText primary={inv.fromUserId} secondary="邀请你入队" />
                    </ListItem>
                  ))}
                </List>
              </Box>
            ) : null}
            <Button variant="contained" onClick={() => void run(() => props.api!.createParty(), '队伍已创建')}>
              创建队伍
            </Button>
            {notice ? <Alert severity={notice.includes('code') ? 'warning' : 'success'}>{notice}</Alert> : null}
          </>
        ) : (
          <>
            <Stack direction="row" spacing={1} alignItems="center">
              <Chip size="small" label={`队长 ${party.leaderId}`} />
              <Chip size="small" label={`${party.members.length}/${party.maxMembers || '∞'}`} />
            </Stack>
            <List dense>
              {party.members.map((m) => (
                <ListItem
                  key={m.userId}
                  secondaryAction={
                    party.leaderId === props.selfId && m.userId !== props.selfId ? (
                      <Stack direction="row" spacing={0.5}>
                        <Button
                          size="small"
                          onClick={() => void run(() => props.api!.transferLeader(m.userId), '队长已转让')}
                        >
                          转让
                        </Button>
                        <Button
                          size="small"
                          color="error"
                          onClick={() => void run(() => props.api!.kickMember(m.userId), '已移出')}
                        >
                          移出
                        </Button>
                      </Stack>
                    ) : null
                  }
                >
                  <ListItemText
                    primary={m.userId}
                    secondary={m.ready ? '已就绪' : '未就绪'}
                  />
                </ListItem>
              ))}
            </List>
            <Stack direction="row" spacing={1}>
              <TextField
                size="small"
                label="邀请用户 ID"
                value={target}
                onChange={(e) => setTarget(e.target.value)}
                sx={{ flex: 1 }}
              />
              <Button
                disabled={!target.trim()}
                onClick={() => {
                  void run(() => props.api!.invite(target.trim()), '邀请已发出');
                  setTarget('');
                }}
              >
                邀请
              </Button>
            </Stack>
            {notice ? <Alert severity={notice.includes('code') ? 'warning' : 'success'}>{notice}</Alert> : null}
            <Stack direction="row" spacing={1}>
              <Button onClick={() => void run(() => props.api!.leaveParty(), '已离队')}>离队</Button>
              {party.leaderId === props.selfId ? (
                <Button color="error" onClick={() => void run(() => props.api!.disbandParty(), '已解散')}>
                  解散
                </Button>
              ) : null}
            </Stack>
          </>
        )}
      </Stack>
    </Shell>
  );
}

/** 语音房面板（voice 实验平面）：建房/加入/离开/闭麦。媒体面未验证——仅信令。 */
export function VoiceDialog(props: {
  api: VoiceApi | null;
  state: Store<VoiceState>;
  open: boolean;
  onClose: () => void;
}) {
  const voiceState = useStoreValue(props.state);
  const [roomId, setRoomId] = useState('');
  const [notice, setNotice] = useState<string | null>(null);
  const room = voiceState.room;

  const run = async (fn: () => Promise<number | boolean>, okText: string): Promise<void> => {
    setNotice(null);
    const code = await fn();
    setNotice(code === 0 || code === true ? okText : `voice 服务返回 code ${code}`);
  };

  return (
    <Shell title="语音房（实验平面 · 信令闭环，媒体面未验证）" open={props.open} onClose={props.onClose}>
      <Stack spacing={2}>
        <Alert severity="info" variant="outlined">
          语音信令（建房/加入/名单/静音）已接 chirp_voice；WebRTC 媒体面端到端仍未验证——
          这里是入口与信令操作，不出声。
        </Alert>
        {!props.api ? (
          <Alert severity="warning">voice 平面未连接（服务未启动或不可达）</Alert>
        ) : !room ? (
          <>
            <Button variant="contained" onClick={() => void run(() => props.api!.createRoom(), '房间已创建')}>
              创建房间
            </Button>
            <Stack direction="row" spacing={1}>
              <TextField
                size="small"
                label="房间 ID"
                value={roomId}
                onChange={(e) => setRoomId(e.target.value)}
                sx={{ flex: 1 }}
              />
              <Button disabled={!roomId.trim()} onClick={() => void run(() => props.api!.joinRoom(roomId.trim()), '已加入')}>
                加入
              </Button>
            </Stack>
          </>
        ) : (
          <>
            <Stack direction="row" spacing={1} alignItems="center">
              <Chip size="small" label={`房间 ${room.roomId}`} />
              <Chip size="small" label={`${room.participants?.length ?? 0} 人`} />
            </Stack>
            <List dense>
              {(room.participants ?? []).map((p) => (
                <ListItem key={p.userId}>
                  <ListItemText
                    primary={p.userId}
                    secondary={[p.muted ? '闭麦' : null, p.deafened ? '拒听' : null]
                      .filter(Boolean)
                      .join(' · ') || '在线'}
                  />
                </ListItem>
              ))}
            </List>
            <Stack direction="row" spacing={1}>
              <Button onClick={() => void run(() => props.api!.setMute(true), '已闭麦')}>闭麦</Button>
              <Button onClick={() => void run(() => props.api!.setMute(false), '已开麦')}>开麦</Button>
              <Button color="error" onClick={() => void run(() => props.api!.leaveRoom(), '已离开房间')}>
                离开
              </Button>
            </Stack>
          </>
        )}
        {notice ? <Alert severity={notice.includes('code') ? 'warning' : 'success'}>{notice}</Alert> : null}
      </Stack>
    </Shell>
  );
}

/** 多端在线清单（同账号同型顶号语义的直观呈现）。 */
export function DevicesDialog(props: {
  state: Store<OnlineDevicesState>;
  selfId: string;
  open: boolean;
  onClose: () => void;
}) {
  const devices = useStoreValue(props.state);
  const list = Object.values(devices.byPlatform);
  return (
    <Shell title="我的在线设备" open={props.open} onClose={props.onClose}>
      {list.length === 0 ? (
        <Typography variant="body2" color="text.secondary">
          暂无其他在线端（本桌面端除外）
        </Typography>
      ) : (
        <List dense>
          {list.map((d) => (
            <ListItem key={d.platform}>
              <ListItemText
                primary={`${d.platform} · ${d.online ? '在线' : '离线'}`}
                secondary={d.deviceId}
              />
            </ListItem>
          ))}
        </List>
      )}
      <Typography variant="caption" color="text.disabled" sx={{ mt: 1, display: 'block' }}>
        同平台新登录会顶掉旧会话（同型顶号），跨平台可共存。
      </Typography>
    </Shell>
  );
}
