import { Paper, Typography } from '@mui/material';
import type { ChatMessageView } from '../state/models';
import { zh } from '../i18n/zh';

const timeLabel = (timestamp: number): string =>
  new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

/** One chat bubble. Mine float right in primary, theirs left in paper. */
export default function MessageBubble({
  message,
  selfId,
}: {
  message: ChatMessageView;
  selfId: string;
}) {
  const mine = message.senderId === selfId;
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
        {!mine && message.edited && ` ${zh.chat.edited}`}
        {!mine && message.deleted && ` ${zh.chat.deleted}`}
      </Typography>
    </Paper>
  );
}
