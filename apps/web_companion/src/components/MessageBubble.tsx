import { Box, Dialog, DialogContent, DialogTitle, Paper, Typography } from '@mui/material';
import DeleteIcon from '@mui/icons-material/DeleteOutlined';
import EditIcon from '@mui/icons-material/EditOutlined';
import FlagIcon from '@mui/icons-material/FlagOutlined';
import ReplyIcon from '@mui/icons-material/ReplyOutlined';
import { useState } from 'react';
import { SenderKind } from '@chirp/proto/chat';
import { QUICK_REACTIONS } from './quick_reactions';
import { expandEmojiCodes } from './emoji';
import { parseRichText } from './rich_text';
import type { ChatMessageView } from '../state/models';
import { zh } from '../i18n/zh';

const timeLabel = (timestamp: number): string =>
  new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

// P0 物品链接：品质配色——白(普通)/绿(优秀)/蓝(精良)/紫(史诗)/橙(传说)。
// 白品质在浅色底上渲染为灰白（可读性适配）；枚举外未知值回落白（向前
// 兼容，与 sender_kind 回落同口径）。
const QUALITY_COLORS: Record<number, string> = {
  0: '#9e9e9e',
  1: '#4caf50',
  2: '#2196f3',
  3: '#9c27b0',
  4: '#ff9800',
};

const qualityColorOf = (quality: number): string => QUALITY_COLORS[quality] ?? QUALITY_COLORS[0];

// P1 成就分享：稀有度分色与物品品质同一阶梯（普通→优秀→稀有→史诗→传说），
// 枚举外未知值回落普通（向前兼容，与品质回落同口径）。
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

// P0 消息展示：气泡底色按 sender_kind 区分——系统黄、NPC 绿、服务橙（交易）；
// 玩家走纸色。自己的消息不受类别影响，恒走 primary 主色（客户端结构上只能
// 以玩家身份发送，故按 mine 收口而非按类别）。tag 落在 data-sender-kind 上
// 供测试断言解析结果。
// Partial: a wire value outside the known enum (UNRECOGNIZED) falls back to
// the player face rather than rendering an undefined bubble color.
const KIND_FACE: Partial<Record<SenderKind, { tag: string; bgcolor: string }>> = {
  [SenderKind.SENDER_USER]: { tag: 'user', bgcolor: 'background.paper' },
  [SenderKind.SENDER_SYSTEM]: { tag: 'system', bgcolor: 'warning.light' },
  [SenderKind.SENDER_NPC]: { tag: 'npc', bgcolor: 'success.light' },
  [SenderKind.SENDER_SERVICE]: { tag: 'service', bgcolor: 'warning.main' },
};

const USER_FACE = { tag: 'user', bgcolor: 'background.paper' };

const faceOf = (message: ChatMessageView, mine: boolean): { tag: string; bgcolor: string } => {
  if (mine) return { tag: 'mine', bgcolor: 'primary.main' };
  return KIND_FACE[message.senderKind as SenderKind] ?? USER_FACE;
};

/** Resolved display face of the quoted parent (ChatWindow resolves the id). */
export interface QuotedSummary {
  senderName: string;
  content: string;
}

export interface MessageBubbleProps {
  message: ChatMessageView;
  selfId: string;
  /** Last message id the peer read; own messages at/below it show 已读. */
  peerReadMessageId?: string;
  /** Group sender attribution (alias, else raw id); private chats pass none. */
  senderName?: string;
  /** Present when the quoted parent is loaded; null = id set but unavailable. */
  quoted?: QuotedSummary | null;
  onToggleReaction: (message: ChatMessageView, emoji: string) => void;
  onReply?: (message: ChatMessageView) => void;
  onEdit?: (message: ChatMessageView) => void;
  onDelete?: (message: ChatMessageView) => void;
  /** Report a peer's message (REPORT_MESSAGE 2253); own messages are never reportable. */
  onReport?: (message: ChatMessageView) => void;
}

/**
 * One chat bubble. Mine float right in primary, theirs left in paper, with
 * reaction chips and (for own messages) edit/delete actions underneath.
 * Group chats show a sender-name caption above peers' bubbles.
 */
export default function MessageBubble({
  message,
  selfId,
  peerReadMessageId,
  senderName,
  quoted,
  onToggleReaction,
  onReply,
  onEdit,
  onDelete,
  onReport,
}: MessageBubbleProps) {
  const mine = message.senderId === selfId;
  // Server message ids are "msg_<ms>_<n>" and monotonic, so string compare
  // orders them correctly; optimistic pending-* ids never compare below.
  const read = mine && !message.pending && !!peerReadMessageId && message.messageId <= peerReadMessageId;
  const [itemOpen, setItemOpen] = useState(false);

  const face = faceOf(message, mine);
  return (
    <Paper
      variant="outlined"
      data-sender-kind={face.tag}
      sx={{
        maxWidth: '72%',
        px: 1.5,
        py: 1,
        my: 0.5,
        ml: mine ? 'auto' : 0,
        mr: mine ? 0 : 'auto',
        bgcolor: face.bgcolor,
        color: mine ? 'primary.contrastText' : 'text.primary',
        opacity: message.pending ? 0.6 : 1,
      }}
    >
      {senderName !== undefined && (
        <Typography variant="caption" sx={{ display: 'block', opacity: 0.75 }}>
          {senderName}
        </Typography>
      )}
      {message.replyToMessageId && (
        <Box
          sx={{
            borderLeft: 3,
            borderColor: mine ? 'primary.contrastText' : 'divider',
            pl: 1,
            mb: 0.5,
            opacity: 0.85,
          }}
          data-testid={`quoted-${message.messageId}`}
        >
          <Typography variant="caption" sx={{ display: 'block', fontWeight: 600 }}>
            {quoted ? `${zh.chat.reply} · ${quoted.senderName}` : zh.chat.quotedUnavailable}
          </Typography>
          {quoted && (
            <Typography
              variant="caption"
              sx={{
                display: '-webkit-box',
                WebkitLineClamp: 2,
                WebkitBoxOrient: 'vertical',
                overflow: 'hidden',
              }}
            >
              {quoted.content}
            </Typography>
          )}
        </Box>
      )}
      <Typography
        variant="body2"
        sx={{ wordBreak: 'break-word', whiteSpace: 'pre-wrap' }}
        data-testid={`msg-text-${message.messageId}`}
      >
        {message.deleted
          ? zh.chat.deleted
          : // P1 富文本：**粗体**/*斜体* 先切样式串，表情码再在每个串内展开
            // （串级样式天然覆盖其中的表情，与 sender_kind 分色互不干扰）。
            parseRichText(message.content).map((run, r) => (
              <Typography
                key={`run-${r}`}
                component={run.bold ? 'strong' : run.italic ? 'em' : 'span'}
                sx={run.bold ? { fontWeight: 700 } : run.italic ? { fontStyle: 'italic' } : undefined}
              >
                {expandEmojiCodes(run.text).map((seg, i) =>
                  seg.kind === 'text' ? (
                    <span key={i}>{seg.value}</span>
                  ) : (
                    <span key={i} data-testid={`emoji-${message.messageId}-${seg.code}`}>
                      {seg.emoji}
                    </span>
                  ),
                )}
              </Typography>
            ))}
      </Typography>
      {!message.deleted && message.coord && (
        <Box
          component="button"
          type="button"
          data-testid={`coord-${message.messageId}`}
          title={zh.chat.copyCoord}
          onClick={() => {
            const { x, y, z } = message.coord!;
            navigator.clipboard?.writeText(z === 0 ? `${x}, ${y}` : `${x}, ${y}, ${z}`);
          }}
          sx={{
            display: 'block',
            width: '100%',
            mb: 0.5,
            px: 1,
            py: 0.5,
            textAlign: 'left',
            cursor: 'pointer',
            border: '1px solid',
            borderColor: mine ? 'primary.contrastText' : 'divider',
            borderRadius: 1,
            background: 'transparent',
            color: 'inherit',
          }}
        >
          {message.coord.label && (
            <Typography variant="caption" sx={{ display: 'block', fontWeight: 600 }}>
              {message.coord.label}
            </Typography>
          )}
          <Typography variant="caption" sx={{ display: 'block', opacity: 0.75 }}>
            {message.coord.mapId}
          </Typography>
          <Typography variant="caption" sx={{ display: 'block', fontFamily: 'monospace' }}>
            {message.coord.z === 0
              ? `${message.coord.x}, ${message.coord.y}`
              : `${message.coord.x}, ${message.coord.y}, ${message.coord.z}`}
          </Typography>
        </Box>
      )}
      {!message.deleted && message.item && (
        <>
          <Box
            component="button"
            type="button"
            data-testid={`item-${message.messageId}`}
            data-quality={message.item.quality}
            title={zh.chat.itemDetails}
            onClick={() => setItemOpen(true)}
            sx={{
              display: 'block',
              width: '100%',
              mb: 0.5,
              px: 1,
              py: 0.5,
              textAlign: 'left',
              cursor: 'pointer',
              border: '1px solid',
              borderColor: mine ? 'primary.contrastText' : 'divider',
              borderRadius: 1,
              background: 'transparent',
              color: 'inherit',
            }}
          >
            <Typography
              variant="caption"
              sx={{ display: 'block', fontWeight: 600, color: qualityColorOf(message.item.quality) }}
            >
              {message.item.itemName || message.item.itemId}
              {message.item.count > 1 && ` ×${message.item.count}`}
            </Typography>
            <Typography variant="caption" sx={{ display: 'block', opacity: 0.75 }}>
              {message.item.itemId}
            </Typography>
          </Box>
          <Dialog
            open={itemOpen}
            onClose={() => setItemOpen(false)}
            data-testid={`item-dialog-${message.messageId}`}
          >
            <DialogTitle sx={{ color: qualityColorOf(message.item.quality) }}>
              {message.item.itemName || message.item.itemId}
            </DialogTitle>
            <DialogContent dividers>
              <Typography variant="caption" sx={{ display: 'block', opacity: 0.75 }}>
                {message.item.itemId}
              </Typography>
              {message.item.iconUrl && (
                <Box
                  component="img"
                  src={message.item.iconUrl}
                  alt={message.item.itemName || message.item.itemId}
                  sx={{ width: 48, height: 48, my: 1, display: 'block' }}
                />
              )}
              {message.item.count > 1 && (
                <Typography variant="caption" sx={{ display: 'block' }}>
                  {zh.chat.itemCountLabel} ×{message.item.count}
                </Typography>
              )}
              {Object.keys(message.item.attrs).length > 0 && (
                <Typography variant="caption" sx={{ display: 'block', fontWeight: 600, mt: 1 }}>
                  {zh.chat.itemAttrsLabel}
                </Typography>
              )}
              {Object.entries(message.item.attrs).map(([key, value]) => (
                <Typography key={key} variant="caption" sx={{ display: 'block' }}>
                  {key}: {value}
                </Typography>
              ))}
            </DialogContent>
          </Dialog>
        </>
      )}
      {!message.deleted && message.achievement && (
        <Box
          data-testid={`achievement-${message.messageId}`}
          data-rarity={message.achievement.rarity}
          sx={{
            display: 'block',
            width: '100%',
            mb: 0.5,
            p: 1,
            border: '1px solid',
            borderColor: mine ? 'primary.contrastText' : 'divider',
            borderRadius: 1,
            background: 'transparent',
            textAlign: 'left',
          }}
        >
          {message.achievement.iconUrl && (
            <Box
              component="img"
              src={message.achievement.iconUrl}
              alt={message.achievement.achievementName || message.achievement.achievementId}
              sx={{ width: 40, height: 40, mb: 0.5, display: 'block' }}
            />
          )}
          <Typography
            variant="caption"
            sx={{ display: 'block', fontWeight: 600, color: rarityColorOf(message.achievement.rarity) }}
          >
            {message.achievement.achievementName || message.achievement.achievementId}
          </Typography>
          <Typography variant="caption" sx={{ display: 'block', opacity: 0.75 }}>
            {zh.chat.achievementRarity} {rarityFaceOf(message.achievement.rarity)}
          </Typography>
          {message.achievement.description && (
            <Typography variant="caption" sx={{ display: 'block', mt: 0.5, opacity: 0.85 }}>
              {message.achievement.description}
            </Typography>
          )}
          <Typography variant="caption" sx={{ display: 'block', opacity: 0.6 }}>
            {message.achievement.achievementId}
          </Typography>
        </Box>
      )}
      {!message.deleted && message.skill && (
        <Box
          data-testid={`skill-${message.messageId}`}
          sx={{
            display: 'block',
            width: '100%',
            mb: 0.5,
            p: 1,
            border: '1px solid',
            borderColor: mine ? 'primary.contrastText' : 'divider',
            borderRadius: 1,
            background: 'transparent',
            textAlign: 'left',
          }}
        >
          {message.skill.iconUrl && (
            <Box
              component="img"
              src={message.skill.iconUrl}
              alt={message.skill.skillName || message.skill.skillId}
              sx={{ width: 40, height: 40, mb: 0.5, display: 'block' }}
            />
          )}
          <Typography variant="caption" sx={{ display: 'block', fontWeight: 600 }}>
            {message.skill.skillName || message.skill.skillId}
            {message.skill.level > 0 && ` Lv.${message.skill.level}`}
          </Typography>
          {message.skill.description && (
            <Typography variant="caption" sx={{ display: 'block', mt: 0.5, opacity: 0.85 }}>
              {message.skill.description}
            </Typography>
          )}
          <Typography variant="caption" sx={{ display: 'block', opacity: 0.6 }}>
            {message.skill.skillId}
          </Typography>
        </Box>
      )}
      {!message.deleted && message.trade && (
        <Box
          data-testid={`trade-${message.messageId}`}
          data-trade-status={message.trade.status}
          sx={{
            display: 'block',
            width: '100%',
            mb: 0.5,
            p: 1,
            border: '1px solid',
            borderColor: mine ? 'primary.contrastText' : 'divider',
            borderRadius: 1,
            background: 'transparent',
            textAlign: 'left',
          }}
        >
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.5 }}>
            <Typography
              variant="caption"
              sx={{
                px: 0.5,
                borderRadius: 0.5,
                bgcolor: 'action.hover',
                fontWeight: 600,
              }}
            >
              {zh.chat.tradeStatus}
              {message.trade.status && ` · ${message.trade.status}`}
            </Typography>
          </Box>
          {(message.trade.itemName || message.trade.amount > 0) && (
            <Typography variant="caption" sx={{ display: 'block', fontWeight: 600, mt: 0.5 }}>
              {message.trade.itemName}
              {message.trade.itemCount > 1 && ` ×${message.trade.itemCount}`}
              {message.trade.itemName && message.trade.amount > 0 && ' · '}
              {message.trade.amount > 0 && `${zh.chat.tradeAmount} ${message.trade.amount}`}
            </Typography>
          )}
          <Typography variant="caption" sx={{ display: 'block', opacity: 0.6 }}>
            {message.trade.tradeId}
          </Typography>
        </Box>
      )}
      {!message.deleted && message.guildRecruit && (
        <Box
          data-testid={`guild-${message.messageId}`}
          sx={{
            display: 'block',
            width: '100%',
            mb: 0.5,
            p: 1,
            border: '1px solid',
            borderColor: mine ? 'primary.contrastText' : 'divider',
            borderRadius: 1,
            background: 'transparent',
            textAlign: 'left',
          }}
        >
          <Typography
            variant="caption"
            component="div"
            sx={{ px: 0.5, borderRadius: 0.5, bgcolor: 'action.hover', fontWeight: 600, width: 'fit-content' }}
          >
            {zh.chat.guildRecruit}
          </Typography>
          <Typography variant="caption" component="div" sx={{ fontWeight: 600, mt: 0.5 }}>
            {message.guildRecruit.guildName || message.guildRecruit.guildId}
          </Typography>
          {(message.guildRecruit.level > 0 || message.guildRecruit.memberCount > 0) && (
            <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
              {message.guildRecruit.level > 0 && `${zh.chat.guildLevel} ${message.guildRecruit.level}`}
              {message.guildRecruit.level > 0 && message.guildRecruit.memberCount > 0 && ' · '}
              {message.guildRecruit.memberCount > 0 &&
                `${zh.chat.guildMembers} ${
                  message.guildRecruit.maxMembers > 0
                    ? `${message.guildRecruit.memberCount}/${message.guildRecruit.maxMembers}`
                    : `${message.guildRecruit.memberCount}`
                }`}
            </Typography>
          )}
          {message.guildRecruit.description && (
            <Typography variant="caption" component="div" sx={{ mt: 0.5, opacity: 0.85 }}>
              {message.guildRecruit.description}
            </Typography>
          )}
          <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
            {message.guildRecruit.guildId}
          </Typography>
        </Box>
      )}
      {!message.deleted && message.partyRecruit && (
        <Box
          data-testid={`party-${message.messageId}`}
          sx={{
            display: 'block',
            width: '100%',
            mb: 0.5,
            p: 1,
            border: '1px solid',
            borderColor: mine ? 'primary.contrastText' : 'divider',
            borderRadius: 1,
            background: 'transparent',
            textAlign: 'left',
          }}
        >
          <Typography
            variant="caption"
            component="div"
            sx={{ px: 0.5, borderRadius: 0.5, bgcolor: 'action.hover', fontWeight: 600, width: 'fit-content' }}
          >
            {zh.chat.partyRecruit}
          </Typography>
          <Typography variant="caption" component="div" sx={{ fontWeight: 600, mt: 0.5 }}>
            {message.partyRecruit.dungeonName || message.partyRecruit.partyId}
          </Typography>
          {message.partyRecruit.needed > 0 && (
            <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
              {`${zh.chat.partyNeeded} ${
                message.partyRecruit.needed -
                (message.partyRecruit.current > 0 ? message.partyRecruit.current : 0)
              } 人`}
              {message.partyRecruit.current > 0 && ` · ${message.partyRecruit.current}/${message.partyRecruit.needed}`}
            </Typography>
          )}
          {message.partyRecruit.leaderName && (
            <Typography variant="caption" component="div" sx={{ opacity: 0.75 }}>
              {`${zh.chat.partyLeader} ${message.partyRecruit.leaderName}`}
            </Typography>
          )}
          <Typography variant="caption" component="div" sx={{ opacity: 0.6 }}>
            {message.partyRecruit.partyId}
          </Typography>
        </Box>
      )}
      <Typography variant="caption" sx={{ opacity: 0.75 }}>
        {timeLabel(message.timestamp)}
        {mine && message.pending && ` · ${zh.chat.statusPending}`}
        {mine &&
          message.failed &&
          ` · ${
            message.failureReason === 'muted'
              ? zh.chat.statusMuted
              : message.failureReason === 'dm_restricted'
                ? zh.chat.statusDmRestricted
                : zh.chat.statusFailed
          }`}
        {mine && message.queuedOffline && ` · ${zh.chat.statusQueued}`}
        {mine && read && ` · ${zh.chat.read}`}
        {!mine && message.edited && ` ${zh.chat.edited}`}
      </Typography>
      <Typography component="div" sx={{ mt: 0.5, display: 'flex', alignItems: 'center', gap: 0.5, flexWrap: 'wrap' }}>
        {!message.deleted &&
          Object.values(message.reactions ?? {}).map((reaction) => (
          <Typography
            key={reaction.emoji}
            component="button"
            data-testid={`reaction-${message.messageId}-${reaction.emoji}`}
            onClick={() => onToggleReaction(message, reaction.emoji)}
            sx={{
              border: '1px solid',
              borderColor: reaction.mine ? 'secondary.main' : 'divider',
              borderRadius: 10,
              background: 'transparent',
              cursor: 'pointer',
              px: 0.5,
              fontSize: 12,
              color: 'inherit',
            }}
          >
            {reaction.emoji}
            {reaction.count > 1 && ` ×${reaction.count}`}
          </Typography>
        ))}
        {!message.deleted && (
          <Typography
            component="span"
            sx={{ display: 'inline-flex', alignItems: 'center', gap: 0.25, ml: 'auto' }}
          >
            {QUICK_REACTIONS.slice(0, 3).map((emoji) => (
              <Typography
                key={emoji}
                component="button"
                data-testid={`react-${message.messageId}-${emoji}`}
                title={zh.chat.addReaction}
                onClick={() => onToggleReaction(message, emoji)}
                sx={{ background: 'transparent', border: 'none', cursor: 'pointer', fontSize: 12, p: 0 }}
              >
                {emoji}
              </Typography>
            ))}
            {onReply && (
              <Typography
                component="button"
                aria-label={zh.chat.reply}
                data-testid={`reply-${message.messageId}`}
                onClick={() => onReply(message)}
                sx={{ background: 'transparent', border: 'none', cursor: 'pointer', fontSize: 11, p: 0 }}
              >
                <ReplyIcon sx={{ fontSize: 13 }} />
              </Typography>
            )}
            {mine && onEdit && (
              <Typography
                component="button"
                aria-label={zh.chat.edit}
                data-testid={`edit-${message.messageId}`}
                onClick={() => onEdit(message)}
                sx={{ background: 'transparent', border: 'none', cursor: 'pointer', fontSize: 11, p: 0 }}
              >
                <EditIcon sx={{ fontSize: 13 }} />
              </Typography>
            )}
            {mine && onDelete && (
              <Typography
                component="button"
                aria-label={zh.chat.delete}
                data-testid={`delete-${message.messageId}`}
                onClick={() => onDelete(message)}
                sx={{ background: 'transparent', border: 'none', cursor: 'pointer', fontSize: 11, p: 0 }}
              >
                <DeleteIcon sx={{ fontSize: 13 }} />
              </Typography>
            )}
            {!mine && !message.deleted && onReport && (
              <Typography
                component="button"
                aria-label={zh.chat.report}
                data-testid={`report-${message.messageId}`}
                onClick={() => onReport(message)}
                sx={{ background: 'transparent', border: 'none', cursor: 'pointer', fontSize: 11, p: 0 }}
              >
                <FlagIcon sx={{ fontSize: 13 }} />
              </Typography>
            )}
          </Typography>
        )}
      </Typography>
    </Paper>
  );
}
