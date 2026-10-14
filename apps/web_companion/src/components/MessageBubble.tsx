import { Box, Paper, Typography } from '@mui/material';
import DeleteIcon from '@mui/icons-material/DeleteOutline';
import EditIcon from '@mui/icons-material/EditOutlined';
import FlagIcon from '@mui/icons-material/FlagOutlined';
import ReplyIcon from '@mui/icons-material/ReplyOutlined';
import { QUICK_REACTIONS } from './quick_reactions';
import type { ChatMessageView } from '../state/models';
import { zh } from '../i18n/zh';

const timeLabel = (timestamp: number): string =>
  new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

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

  return (
    <Paper
      variant="outlined"
      sx={{
        maxWidth: '72%',
        px: 1.5,
        py: 1,
        my: 0.5,
        ml: mine ? 'auto' : 0,
        mr: mine ? 0 : 'auto',
        bgcolor: mine ? 'primary.main' : 'background.paper',
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
      <Typography variant="body2" sx={{ wordBreak: 'break-word', whiteSpace: 'pre-wrap' }}>
        {message.deleted ? zh.chat.deleted : message.content}
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
      <Typography variant="caption" sx={{ opacity: 0.75 }}>
        {timeLabel(message.timestamp)}
        {mine && message.pending && ` · ${zh.chat.statusPending}`}
        {mine &&
          message.failed &&
          ` · ${message.failureReason === 'muted' ? zh.chat.statusMuted : zh.chat.statusFailed}`}
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
