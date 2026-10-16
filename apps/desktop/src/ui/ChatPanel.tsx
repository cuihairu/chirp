import ArrowUpwardIcon from '@mui/icons-material/ArrowUpward';
import CloseIcon from '@mui/icons-material/Close';
import EmojiEmotionsIcon from '@mui/icons-material/EmojiEmotionsOutlined';
import FlagIcon from '@mui/icons-material/FlagOutlined';
import ReplyIcon from '@mui/icons-material/ReplyOutlined';
import SendIcon from '@mui/icons-material/Send';
import {
  Box,
  Button,
  Chip,
  CircularProgress,
  Dialog,
  DialogContent,
  DialogTitle,
  Divider,
  IconButton,
  InputBase,
  Paper,
  Popover,
  Stack,
  Tooltip,
  Typography,
} from '@mui/material';
import { useEffect, useReducer, useRef, useState } from 'react';
import { SenderKind } from '@chirp/proto/chat';
import type { KeyboardEvent as ReactKeyboardEvent } from 'react';
import type { ChatApi, ChannelRef } from '../api/chat_api';
import type {
  AchievementView,
  ChatMessageView,
  Conversation,
  CoordView,
  GuildRecruitView,
  ItemView,
  PartyRecruitView,
  SkillView,
  TradeView,
} from '../state/models';
import type { MessageState } from '../state/message_store';
import { useStoreValue, type Store } from '../state/store';
import { TYPING_TTL_MS, type TypingState } from '../state/typing_store';
import { expandEmojiCodes, SYSTEM_EMOJIS } from './emoji';
import { parseRichText } from './rich_text';

const fmtTime = (ts: number): string => {
  const d = new Date(ts);
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  return `${hh}:${mm}`;
};

// P0 消息展示：气泡底色按 sender_kind 区分——系统黄、NPC 绿、服务橙（交易）；
// 玩家走纸色。自己的消息不受类别影响，恒走主色（客户端结构上只能以玩家
// 身份发送，故按 mine 收口而非按类别）。tag 落在 data-sender-kind 上供测试
// 断言解析结果。与 web_companion 的 MessageBubble 同款口径。
// Partial: a wire value outside the known enum (UNRECOGNIZED) falls back to
// the player face rather than rendering an undefined bubble color.
const KIND_FACE: Partial<Record<SenderKind, { tag: string; bgcolor: string }>> = {
  [SenderKind.SENDER_USER]: { tag: 'user', bgcolor: 'background.paper' },
  [SenderKind.SENDER_SYSTEM]: { tag: 'system', bgcolor: 'warning.light' },
  [SenderKind.SENDER_NPC]: { tag: 'npc', bgcolor: 'success.light' },
  [SenderKind.SENDER_SERVICE]: { tag: 'service', bgcolor: 'warning.main' },
};

const USER_FACE = { tag: 'user', bgcolor: 'background.paper' };

// P0 物品链接：品质配色——白(普通)/绿(优秀)/蓝(精良)/紫(史诗)/橙(传说)。
// 白品质在浅色底上渲染为灰白（可读性适配）；枚举外未知值回落白（向前
// 兼容，与 sender_kind 回落同口径）。与 web_companion 的 MessageBubble 同款。
const QUALITY_COLORS: Record<number, string> = {
  0: '#9e9e9e',
  1: '#4caf50',
  2: '#2196f3',
  3: '#9c27b0',
  4: '#ff9800',
};

const qualityColorOf = (quality: number): string => QUALITY_COLORS[quality] ?? QUALITY_COLORS[0];

// P1 成就分享：稀有度分色与物品品质同一阶梯（普通→优秀→稀有→史诗→传说），
// 枚举外未知值回落普通（向前兼容）。与 web_companion 的 MessageBubble 同款。
const RARITY_COLORS: Record<number, string> = {
  0: '#9e9e9e',
  1: '#4caf50',
  2: '#2196f3',
  3: '#9c27b0',
  4: '#ff9800',
};

const RARITY_FACES: Record<number, string> = {
  0: '普通',
  1: '优秀',
  2: '稀有',
  3: '史诗',
  4: '传说',
};

const rarityColorOf = (rarity: number): string => RARITY_COLORS[rarity] ?? RARITY_COLORS[0];

const rarityFaceOf = (rarity: number): string => RARITY_FACES[rarity] ?? RARITY_FACES[0];

const faceOf = (props: { mine: boolean; senderKind?: SenderKind }): { tag: string; bgcolor: string } => {
  if (props.mine) return { tag: 'mine', bgcolor: 'primary.main' };
  return KIND_FACE[props.senderKind as SenderKind] ?? USER_FACE;
};

/** 单条消息行：发送者 + 时间 + 正文 + 本地状态标记 + 引用块/回复入口。 */
function MessageRow(props: {
  /** 消息 id：卡片/操作的 testid 锚点。 */
  messageId: string;
  mine: boolean;
  senderId: string;
  /** 群聊归属名（群昵称优先，回退本名）；私聊不传。 */
  senderName?: string;
  content: string;
  timestamp: number;
  pending: boolean;
  failed?: boolean;
  failureReason?: 'muted' | 'dm_restricted';
  queuedOffline?: boolean;
  edited?: boolean;
  readByPeer: boolean;
  /** 被引用父消息的展示面（ChatPanel 解析 id）；null = 引用目标不可见。 */
  quoted?: { senderName: string; content: string } | null;
  replyToMessageId?: string;
  /** 坐标卡片（P1 COORD_LINK）；缺省 = 纯文本气泡。点击复制坐标。 */
  coord?: CoordView;
  /** 物品卡片（P0 ITEM_LINK）；缺省 = 纯文本气泡。点击打开详情弹窗。 */
  item?: ItemView;
  /** 成就卡片（P1 ACHIEVEMENT）；缺省 = 纯文本气泡。图标+名称+稀有度+描述。 */
  achievement?: AchievementView;
  /** 技能卡片（P1 SKILL_LINK）；缺省 = 纯文本气泡。图标+名称+等级+描述。 */
  skill?: SkillView;
  /** 交易卡片（P1 TRADE_STATUS）；缺省 = 纯文本气泡。状态徽章+物品摘要+金额。 */
  trade?: TradeView;
  /** 公会招募卡片（P1 GUILD_RECRUIT）；缺省 = 纯文本气泡。展示性卡片。 */
  guildRecruit?: GuildRecruitView;
  /** 组队招募卡片（P1 PARTY_RECRUIT）；缺省 = 纯文本气泡。展示性卡片。 */
  partyRecruit?: PartyRecruitView;
  /** 发送者类别（P0 消息展示）：决定气泡底色，自己的消息恒走主色。 */
  senderKind?: SenderKind;
  onReply?: () => void;
  /** 举报对方消息（REPORT_MESSAGE 2253）；自己的消息不可举报。 */
  onReport?: () => void;
}) {
  const [itemOpen, setItemOpen] = useState(false);
  return (
    <Box sx={{ display: 'flex', justifyContent: props.mine ? 'flex-end' : 'flex-start', mb: 1 }}>
      <Box sx={{ maxWidth: '72%' }}>
        {!props.mine ? (
          <Typography variant="caption" color="text.secondary">
            {props.senderName ?? props.senderId}
          </Typography>
        ) : null}
        <Paper
          elevation={0}
          data-sender-kind={faceOf(props).tag}
          sx={{
            px: 1.5,
            py: 1,
            bgcolor: faceOf(props).bgcolor,
            color: props.mine ? '#08130f' : 'text.primary',
            opacity: props.pending ? 0.6 : 1,
            border: props.failed ? '1px solid #e57373' : 'none',
            mt: props.mine ? 0 : 0.25,
          }}
        >
          {props.replyToMessageId ? (
            <Box
              sx={{
                borderLeft: 3,
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                pl: 1,
                mb: 0.5,
                opacity: 0.85,
              }}
            >
              <Typography variant="caption" component="div" sx={{ fontWeight: 600 }}>
                {props.quoted ? `回复 · ${props.quoted.senderName}` : '原消息已删除或不可见'}
              </Typography>
              {props.quoted ? (
                <Typography
                  variant="caption"
                  sx={{
                    display: '-webkit-box',
                    WebkitLineClamp: 2,
                    WebkitBoxOrient: 'vertical',
                    overflow: 'hidden',
                  }}
                >
                  {props.quoted.content}
                </Typography>
              ) : null}
            </Box>
          ) : null}
          <Typography
            variant="body2"
            sx={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}
            data-testid={`msg-text-${props.messageId}`}
          >
            {/* P1 富文本：**粗体** 与 *斜体* 先切样式串，表情码再在每个串内
                展开（串级样式天然覆盖其中的表情）。 */}
            {parseRichText(props.content).map((run, r) => (
              <Typography
                key={`run-${r}`}
                component={run.bold ? 'strong' : run.italic ? 'em' : 'span'}
                sx={run.bold ? { fontWeight: 700 } : run.italic ? { fontStyle: 'italic' } : undefined}
              >
                {expandEmojiCodes(run.text).map((seg, i) =>
                  seg.kind === 'text' ? (
                    <span key={i}>{seg.value}</span>
                  ) : (
                    <span key={i} data-testid={`emoji-${props.messageId}-${seg.code}`}>
                      {seg.emoji}
                    </span>
                  ),
                )}
              </Typography>
            ))}
          </Typography>
          {props.coord ? (
            <Box
              component="button"
              type="button"
              title="点击复制坐标"
              onClick={() => {
                const { x, y, z } = props.coord!;
                navigator.clipboard?.writeText(z === 0 ? `${x}, ${y}` : `${x}, ${y}, ${z}`);
              }}
              sx={{
                display: 'block',
                width: '100%',
                mt: 0.5,
                px: 1,
                py: 0.5,
                textAlign: 'left',
                cursor: 'pointer',
                border: '1px solid',
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                borderRadius: 1,
                background: 'transparent',
                color: 'inherit',
              }}
            >
              {props.coord.label ? (
                <Typography variant="caption" component="div" sx={{ fontWeight: 600 }}>
                  {props.coord.label}
                </Typography>
              ) : null}
              <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                {props.coord.mapId}
              </Typography>
              <Typography variant="caption" component="div" sx={{ fontFamily: 'monospace' }}>
                {props.coord.z === 0
                  ? `${props.coord.x}, ${props.coord.y}`
                  : `${props.coord.x}, ${props.coord.y}, ${props.coord.z}`}
              </Typography>
            </Box>
          ) : null}
          {props.item ? (
            <>
              <Box
                component="button"
                type="button"
                data-testid={`item-${props.messageId}`}
                data-quality={props.item.quality}
                title="物品详情"
                onClick={() => setItemOpen(true)}
                sx={{
                  display: 'block',
                  width: '100%',
                  mt: 0.5,
                  px: 1,
                  py: 0.5,
                  textAlign: 'left',
                  cursor: 'pointer',
                  border: '1px solid',
                  borderColor: props.mine ? 'primary.contrastText' : 'divider',
                  borderRadius: 1,
                  background: 'transparent',
                  color: 'inherit',
                }}
              >
                <Typography
                  variant="caption"
                  component="div"
                  sx={{ fontWeight: 600, color: qualityColorOf(props.item.quality) }}
                >
                  {props.item.itemName || props.item.itemId}
                  {props.item.count > 1 ? ` ×${props.item.count}` : ''}
                </Typography>
                <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                  {props.item.itemId}
                </Typography>
              </Box>
              <Dialog open={itemOpen} onClose={() => setItemOpen(false)}>
                <DialogTitle sx={{ color: qualityColorOf(props.item.quality) }}>
                  {props.item.itemName || props.item.itemId}
                </DialogTitle>
                <DialogContent dividers>
                  <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                    {props.item.itemId}
                  </Typography>
                  {props.item.iconUrl ? (
                    <Box
                      component="img"
                      src={props.item.iconUrl}
                      alt={props.item.itemName || props.item.itemId}
                      sx={{ width: 48, height: 48, my: 1, display: 'block' }}
                    />
                  ) : null}
                  {props.item.count > 1 ? (
                    <Typography variant="caption" component="div">
                      {`数量 ×${props.item.count}`}
                    </Typography>
                  ) : null}
                  {Object.keys(props.item.attrs).length > 0 ? (
                    <Typography variant="caption" component="div" sx={{ fontWeight: 600, mt: 1 }}>
                      属性
                    </Typography>
                  ) : null}
                  {Object.entries(props.item.attrs).map(([key, value]) => (
                    <Typography key={key} variant="caption" component="div">
                      {`${key}: ${value}`}
                    </Typography>
                  ))}
                </DialogContent>
              </Dialog>
            </>
          ) : null}
          {props.achievement ? (
            <Box
              data-testid={`achievement-${props.messageId}`}
              data-rarity={props.achievement.rarity}
              sx={{
                display: 'block',
                width: '100%',
                mt: 0.5,
                p: 1,
                textAlign: 'left',
                border: '1px solid',
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                borderRadius: 1,
                background: 'transparent',
                color: 'inherit',
              }}
            >
              {props.achievement.iconUrl ? (
                <Box
                  component="img"
                  src={props.achievement.iconUrl}
                  alt={props.achievement.achievementName || props.achievement.achievementId}
                  sx={{ width: 40, height: 40, mb: 0.5, display: 'block' }}
                />
              ) : null}
              <Typography
                variant="caption"
                component="div"
                sx={{ fontWeight: 600, color: rarityColorOf(props.achievement.rarity) }}
              >
                {props.achievement.achievementName || props.achievement.achievementId}
              </Typography>
              <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                {`稀有度 ${rarityFaceOf(props.achievement.rarity)}`}
              </Typography>
              {props.achievement.description ? (
                <Typography variant="caption" component="div" sx={{ mt: 0.5, opacity: 0.85 }}>
                  {props.achievement.description}
                </Typography>
              ) : null}
              <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
                {props.achievement.achievementId}
              </Typography>
            </Box>
          ) : null}
          {props.skill ? (
            <Box
              data-testid={`skill-${props.messageId}`}
              sx={{
                display: 'block',
                width: '100%',
                mt: 0.5,
                p: 1,
                textAlign: 'left',
                border: '1px solid',
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                borderRadius: 1,
                background: 'transparent',
                color: 'inherit',
              }}
            >
              {props.skill.iconUrl ? (
                <Box
                  component="img"
                  src={props.skill.iconUrl}
                  alt={props.skill.skillName || props.skill.skillId}
                  sx={{ width: 40, height: 40, mb: 0.5, display: 'block' }}
                />
              ) : null}
              <Typography variant="caption" component="div" sx={{ fontWeight: 600 }}>
                {props.skill.skillName || props.skill.skillId}
                {props.skill.level > 0 ? ` Lv.${props.skill.level}` : ''}
              </Typography>
              {props.skill.description ? (
                <Typography variant="caption" component="div" sx={{ mt: 0.5, opacity: 0.85 }}>
                  {props.skill.description}
                </Typography>
              ) : null}
              <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
                {props.skill.skillId}
              </Typography>
            </Box>
          ) : null}
          {props.trade ? (
            <Box
              data-testid={`trade-${props.messageId}`}
              data-trade-status={props.trade.status}
              sx={{
                display: 'block',
                width: '100%',
                mt: 0.5,
                p: 1,
                textAlign: 'left',
                border: '1px solid',
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                borderRadius: 1,
                background: 'transparent',
                color: 'inherit',
              }}
            >
              <Typography
                variant="caption"
                component="div"
                sx={{ px: 0.5, borderRadius: 0.5, bgcolor: 'action.hover', fontWeight: 600, width: 'fit-content' }}
              >
                {props.trade.status ? `交易状态 · ${props.trade.status}` : '交易状态'}
              </Typography>
              {props.trade.itemName || props.trade.amount > 0 ? (
                <Typography variant="caption" component="div" sx={{ fontWeight: 600, mt: 0.5 }}>
                  {props.trade.itemName}
                  {props.trade.itemCount > 1 ? ` ×${props.trade.itemCount}` : ''}
                  {props.trade.itemName && props.trade.amount > 0 ? ' · ' : ''}
                  {props.trade.amount > 0 ? `金额 ${props.trade.amount}` : ''}
                </Typography>
              ) : null}
              <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
                {props.trade.tradeId}
              </Typography>
            </Box>
          ) : null}
          {props.guildRecruit ? (
            <Box
              data-testid={`guild-${props.messageId}`}
              sx={{
                display: 'block',
                width: '100%',
                mt: 0.5,
                p: 1,
                textAlign: 'left',
                border: '1px solid',
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                borderRadius: 1,
                background: 'transparent',
                color: 'inherit',
              }}
            >
              <Typography
                variant="caption"
                component="div"
                sx={{ px: 0.5, borderRadius: 0.5, bgcolor: 'action.hover', fontWeight: 600, width: 'fit-content' }}
              >
                公会招募
              </Typography>
              <Typography variant="caption" component="div" sx={{ fontWeight: 600, mt: 0.5 }}>
                {props.guildRecruit.guildName || props.guildRecruit.guildId}
              </Typography>
              {props.guildRecruit.level > 0 || props.guildRecruit.memberCount > 0 ? (
                <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                  {props.guildRecruit.level > 0 ? `等级 ${props.guildRecruit.level}` : ''}
                  {props.guildRecruit.level > 0 && props.guildRecruit.memberCount > 0 ? ' · ' : ''}
                  {props.guildRecruit.memberCount > 0
                    ? `人数 ${
                        props.guildRecruit.maxMembers > 0
                          ? `${props.guildRecruit.memberCount}/${props.guildRecruit.maxMembers}`
                          : `${props.guildRecruit.memberCount}`
                      }`
                    : ''}
                </Typography>
              ) : null}
              {props.guildRecruit.description ? (
                <Typography variant="caption" component="div" sx={{ mt: 0.5, opacity: 0.85 }}>
                  {props.guildRecruit.description}
                </Typography>
              ) : null}
              <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
                {props.guildRecruit.guildId}
              </Typography>
            </Box>
          ) : null}
          {props.partyRecruit ? (
            <Box
              data-testid={`party-${props.messageId}`}
              sx={{
                display: 'block',
                width: '100%',
                mt: 0.5,
                p: 1,
                textAlign: 'left',
                border: '1px solid',
                borderColor: props.mine ? 'primary.contrastText' : 'divider',
                borderRadius: 1,
                background: 'transparent',
                color: 'inherit',
              }}
            >
              <Typography
                variant="caption"
                component="div"
                sx={{ px: 0.5, borderRadius: 0.5, bgcolor: 'action.hover', fontWeight: 600, width: 'fit-content' }}
              >
                组队招募
              </Typography>
              <Typography variant="caption" component="div" sx={{ fontWeight: 600, mt: 0.5 }}>
                {props.partyRecruit.dungeonName || props.partyRecruit.partyId}
              </Typography>
              {props.partyRecruit.needed > 0 ? (
                <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                  {`缺 ${
                    props.partyRecruit.needed - (props.partyRecruit.current > 0 ? props.partyRecruit.current : 0)
                  } 人`}
                  {props.partyRecruit.current > 0 ? ` · ${props.partyRecruit.current}/${props.partyRecruit.needed}` : ''}
                </Typography>
              ) : null}
              {props.partyRecruit.leaderName ? (
                <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
                  {`队长 ${props.partyRecruit.leaderName}`}
                </Typography>
              ) : null}
              <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
                {props.partyRecruit.partyId}
              </Typography>
            </Box>
          ) : null}
        </Paper>
        <Stack
          direction="row"
          spacing={0.5}
          sx={{ justifyContent: props.mine ? 'flex-end' : 'flex-start', mt: 0.25, alignItems: 'center' }}
        >
          <Typography variant="caption" color="text.disabled">
            {fmtTime(props.timestamp)}
            {props.edited ? ' · 已编辑' : ''}
          </Typography>
          {props.onReply ? (
            <Tooltip title="回复">
              <IconButton size="small" aria-label="回复" onClick={props.onReply} sx={{ p: 0.25 }}>
                <ReplyIcon sx={{ fontSize: 14 }} />
              </IconButton>
            </Tooltip>
          ) : null}
          {!props.mine && props.onReport ? (
            <Tooltip title="举报">
              <IconButton size="small" aria-label="举报" onClick={props.onReport} sx={{ p: 0.25 }}>
                <FlagIcon sx={{ fontSize: 14 }} />
              </IconButton>
            </Tooltip>
          ) : null}
          {props.pending ? (
            <Typography variant="caption" color="text.disabled">
              发送中
            </Typography>
          ) : null}
          {props.failed ? (
            <Typography variant="caption" color="error">
              {props.failureReason === 'muted'
                ? '禁言中，消息未发送'
                : props.failureReason === 'dm_restricted'
                  ? '对方开启了私聊限制，仅好友/同群可私信'
                  : '发送失败'}
            </Typography>
          ) : null}
          {props.queuedOffline ? (
            <Typography variant="caption" color="text.secondary">
              对方离线，已入离线队列
            </Typography>
          ) : null}
          {props.mine && props.readByPeer && !props.pending && !props.failed ? (
            <Typography variant="caption" color="text.disabled">
              已读
            </Typography>
          ) : null}
        </Stack>
      </Box>
    </Box>
  );
}

/**
 * 聊天主面板：历史（分页加载）+ 实时收发 + 已读回执 + 正在输入 +
 * 离线队列提示。stores 经 services 注入（与 web_companion 同构）。
 */
export default function ChatPanel(props: {
  api: ChatApi;
  messages: Store<MessageState>;
  typing: Store<TypingState>;
  conversation: Conversation;
  selfId: string;
  channel: ChannelRef;
  membersHint?: string[];
}) {
  const { api, conversation, selfId, channel } = props;
  const messages = useStoreValue(props.messages);
  const typing = useStoreValue(props.typing);
  const list = messages.byChannel[conversation.key] ?? [];
  const hasMore = messages.hasMore[conversation.key] ?? false;
  const loading = messages.loadingHistory[conversation.key] ?? false;
  const [draft, setDraft] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [replying, setReplying] = useState<ChatMessageView | null>(null);
  const [emojiAnchor, setEmojiAnchor] = useState<HTMLElement | null>(null);
  const bottomRef = useRef<HTMLDivElement | null>(null);
  const cursors = messages.readCursors[conversation.key] ?? {};

  // 打开会话即拉历史并标读（要带最新消息 id，对端的「已读」才能对上号）；
  // 离开会话时清 activeChannel（免得本会话外的消息再涨本地未读红点）。
  useEffect(() => {
    setError(null);
    setReplying(null);
    void api.loadHistory(channel).then(() => {
      const fresh = props.messages.get().byChannel[conversation.key] ?? [];
      const latest = fresh[fresh.length - 1];
      if (latest) void api.markRead(channel, latest.messageId);
    });
    api.setActiveChannel(conversation.key);
    return () => api.setActiveChannel(null);
  }, [api, channel, conversation.key]);

  // 会话保持打开期间，每条新到的消息都即时标读。
  useEffect(() => {
    const newest = list[list.length - 1];
    if (!newest || newest.senderId === selfId) return;
    void api.markRead(channel, newest.messageId);
  }, [api, channel, selfId, list]);

  // 群会话：名册随群打开拉一次（昵称快照由此建立）；2124/名册刷新即重渲发送者名。
  const [, bumpAlias] = useReducer((n: number) => n + 1, 0);
  useEffect(() => {
    if (channel.kind !== 'group') return;
    void api.loadGroupMembers(channel.channelId);
    return api.subscribeGroupAlias(bumpAlias);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [channel.kind, channel.channelId]);

  // 新消息自动滚底（v1 从简：总是滚）。
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ block: 'end' });
  }, [list.length]);

  /** 回复条/引用块的归属名：群聊昵称优先；私聊自己显示「我」。 */
  const displayName = (uid: string): string =>
    channel.kind === 'group'
      ? api.groupAliasOf(channel.channelId, uid) || uid
      : uid === selfId
        ? '我'
        : uid;

  /** 解析被引用父消息为展示面；不存在或已撤回 → null（显示不可见兜底）。 */
  const quotedSummary = (id: string): { senderName: string; content: string } | null => {
    const src = list.find((m) => m.messageId === id);
    if (!src || src.deleted) return null;
    return { senderName: displayName(src.senderId), content: src.content };
  };

  const send = async (): Promise<void> => {
    const text = draft.trim();
    if (!text) return;
    setDraft('');
    setError(null);
    try {
      await api.sendMessage(channel, text, replying?.messageId || undefined);
      setReplying(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  };

  const onDraftKeyDown = (event: ReactKeyboardEvent<HTMLInputElement | HTMLTextAreaElement>): void => {
    if (event.key === 'Enter' && !event.shiftKey && !event.nativeEvent.isComposing) {
      event.preventDefault();
      void send();
    }
  };

  const lastMessage = list[list.length - 1];

  // 已读回执：对方已读游标追平最新一条时，我发的行标「已读」（与
  // web_companion 同一语义；群聊暂无成员游标，不显示）。
  const peerRead =
    conversation.kind === 'private' && list.length > 0
      ? cursors[conversation.peerId] === lastMessage?.messageId
      : false;

  // 正在输入：TTL 内的用户，排除自己。
  const now = Date.now();
  const typingUsers = Object.entries(typing.byChannel[conversation.key] ?? {})
    .filter(([uid, at]) => uid !== selfId && now - at < TYPING_TTL_MS)
    .map(([uid]) => uid);

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', height: '100%', minWidth: 0 }}>
      <Box
        sx={{
          px: 2,
          py: 1.5,
          bgcolor: 'background.paper',
          borderBottom: '1px solid',
          borderColor: 'divider',
        }}
      >
        <Stack direction="row" spacing={1} sx={{ alignItems: 'center' }}>
          <Typography variant="subtitle1" noWrap sx={{ fontWeight: 600 }}>
            {conversation.title}
          </Typography>
          <Chip
            size="small"
            label={conversation.kind === 'private' ? '私聊' : '群聊'}
            variant="outlined"
          />
          {props.membersHint ? (
            <Typography variant="caption" color="text.secondary" noWrap>
              {props.membersHint.length} 名成员
            </Typography>
          ) : null}
        </Stack>
      </Box>

      <Box sx={{ flex: 1, overflowY: 'auto', px: 2, py: 1.5 }}>
        {hasMore ? (
          <Box sx={{ textAlign: 'center', mb: 1 }}>
            <Button
              size="small"
              startIcon={
                loading ? <CircularProgress size={12} /> : <ArrowUpwardIcon fontSize="small" />
              }
              onClick={() => void api.loadHistory(channel, list[0]?.timestamp)}
              disabled={loading}
            >
              加载更早消息
            </Button>
          </Box>
        ) : null}
        {list.map((m) => (
          <MessageRow
            key={m.messageId}
            messageId={m.messageId}
            mine={m.senderId === selfId}
            senderId={m.senderId}
            senderName={
              channel.kind === 'group'
                ? api.groupAliasOf(channel.channelId, m.senderId) || m.senderId
                : undefined
            }
            content={m.content}
            timestamp={m.timestamp}
            pending={m.pending}
            failed={m.failed}
            failureReason={m.failureReason}
            queuedOffline={m.queuedOffline}
            edited={m.edited}
            readByPeer={peerRead}
            quoted={m.replyToMessageId ? quotedSummary(m.replyToMessageId) : undefined}
            replyToMessageId={m.replyToMessageId}
            coord={m.coord}
            item={m.deleted ? undefined : m.item}
            achievement={m.deleted ? undefined : m.achievement}
            skill={m.deleted ? undefined : m.skill}
            trade={m.deleted ? undefined : m.trade}
            guildRecruit={m.deleted ? undefined : m.guildRecruit}
            partyRecruit={m.deleted ? undefined : m.partyRecruit}
            senderKind={m.senderKind}
            onReply={m.deleted ? undefined : () => setReplying(m)}
            onReport={m.deleted || m.senderId === selfId ? undefined : () => {
              if (window.confirm('举报这条消息?')) void api.reportMessage(m.messageId);
            }}
          />
        ))}
        <div ref={bottomRef} />
      </Box>

      {typingUsers.length > 0 ? (
        <Typography variant="caption" color="text.secondary" sx={{ px: 2, pb: 0.5 }}>
          {typingUsers.join('、')} 正在输入…
        </Typography>
      ) : null}

      {error ? (
        <Typography variant="caption" color="error" sx={{ px: 2, pb: 0.5 }}>
          {error}
        </Typography>
      ) : null}

      <Divider />
      <Box sx={{ p: 1.5, bgcolor: 'background.paper' }}>
        {replying ? (
          <Stack
            direction="row"
            spacing={0.5}
            sx={{ alignItems: 'center', mb: 0.5, px: 1, py: 0.25, bgcolor: 'action.hover', borderRadius: 1 }}
          >
            <Typography
              variant="caption"
              color="text.secondary"
              noWrap
              sx={{ flex: 1, minWidth: 0 }}
            >
              回复 · {displayName(replying.senderId)}: {replying.content}
            </Typography>
            <IconButton size="small" aria-label="取消回复" onClick={() => setReplying(null)} sx={{ p: 0.25 }}>
              <CloseIcon sx={{ fontSize: 14 }} />
            </IconButton>
          </Stack>
        ) : null}
        <Stack direction="row" spacing={1} sx={{ alignItems: 'flex-end' }}>
          <InputBase
            fullWidth
            multiline
            maxRows={5}
            placeholder={`发消息给 ${conversation.title}（Enter 发送，Shift+Enter 换行）`}
            value={draft}
            onChange={(e) => {
              setDraft(e.target.value);
              api.sendTyping(channel, e.target.value.length > 0);
            }}
            onKeyDown={onDraftKeyDown}
            sx={{ bgcolor: 'background.default', borderRadius: 2, px: 1.5, py: 0.75 }}
          />
          <Tooltip title="表情">
            <IconButton
              aria-label="表情"
              data-testid="emoji-picker-toggle"
              onClick={(e) => setEmojiAnchor(e.currentTarget)}
            >
              <EmojiEmotionsIcon />
            </IconButton>
          </Tooltip>
          <Popover
            open={emojiAnchor !== null}
            anchorEl={emojiAnchor}
            onClose={() => setEmojiAnchor(null)}
            anchorOrigin={{ vertical: 'top', horizontal: 'right' }}
            transformOrigin={{ vertical: 'bottom', horizontal: 'right' }}
          >
            <Box sx={{ display: 'grid', gridTemplateColumns: 'repeat(8, 36px)', gap: 0.25, p: 1 }}>
              {SYSTEM_EMOJIS.map((e) => (
                <IconButton
                  key={e.code}
                  data-testid={`emoji-pick-${e.code}`}
                  onClick={() => {
                    setDraft((d) => `${d}[emoji:${e.code}]`);
                    setEmojiAnchor(null);
                  }}
                  sx={{ fontSize: 18, borderRadius: 1 }}
                >
                  {e.emoji}
                </IconButton>
              ))}
            </Box>
          </Popover>
          <Tooltip title="发送">
            <span>
              <IconButton color="primary" onClick={() => void send()} disabled={!draft.trim()}>
                <SendIcon />
              </IconButton>
            </span>
          </Tooltip>
        </Stack>
      </Box>
    </Box>
  );
}
