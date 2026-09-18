import { Paper, Typography } from '@mui/material';
import DeleteIcon from '@mui/icons-material/DeleteOutline';
import EditIcon from '@mui/icons-material/EditOutlined';
import { QUICK_REACTIONS } from './quick_reactions';
import type { ChatMessageView } from '../state/models';
import { zh } from '../i18n/zh';

const timeLabel = (timestamp: number): string =>
  new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

export interface MessageBubbleProps {
  message: ChatMessageView;
  selfId: string;
  /** Last message id the peer read; own messages at/below it show 已读. */
  peerReadMessageId?: string;
  onToggleReaction: (message: ChatMessageView, emoji: string) => void;
  onEdit?: (message: ChatMessageView) => void;
  onDelete?: (message: ChatMessageView) => void;
}

/**
 * One chat bubble. Mine float right in primary, theirs left in paper, with
 * reaction chips and (for own messages) edit/delete actions underneath.
 */
export default function MessageBubble({
  message,
  selfId,
  peerReadMessageId,
  onToggleReaction,
  onEdit,
  onDelete,
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
      <Typography variant="body2" sx={{ wordBreak: 'break-word', whiteSpace: 'pre-wrap' }}>
        {message.deleted ? zh.chat.deleted : message.content}
      </Typography>
      <Typography variant="caption" sx={{ opacity: 0.75 }}>
        {timeLabel(message.timestamp)}
        {mine && message.pending && ` · ${zh.chat.statusPending}`}
        {mine && message.failed && ` · ${zh.chat.statusFailed}`}
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
          </Typography>
        )}
      </Typography>
    </Paper>
  );
}
