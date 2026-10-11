import { useState } from 'react';
import type { KeyboardEvent } from 'react';
import { Box, IconButton, InputAdornment, TextField, Typography } from '@mui/material';
import CloseIcon from '@mui/icons-material/Close';
import SendIcon from '@mui/icons-material/Send';
import { zh } from '../i18n/zh';

/** Presentational face of the pending reply (ChatWindow owns the state). */
export interface ReplyPreview {
  senderName: string;
  content: string;
}

/** Message composer: Enter sends, Shift+Enter breaks a line. */
export default function MessageInput({
  onSend,
  onTyping,
  disabled = false,
  replyPreview = null,
  onCancelReply,
}: {
  onSend: (text: string) => void;
  /** Fired on every keystroke; the parent owns the typing throttle. */
  onTyping?: () => void;
  disabled?: boolean;
  /** Quoted parent shown above the field while a reply is pending. */
  replyPreview?: ReplyPreview | null;
  onCancelReply?: () => void;
}) {
  const [text, setText] = useState('');

  const submit = (): void => {
    const trimmed = text.trim();
    if (!trimmed) return;
    onSend(trimmed);
    setText('');
  };

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>): void => {
    if (event.key === 'Enter' && !event.shiftKey && !event.nativeEvent.isComposing) {
      event.preventDefault();
      submit();
    }
  };

  return (
    <Box>
      {replyPreview && (
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 0.5,
            px: 1,
            py: 0.25,
            mb: 0.5,
            bgcolor: 'action.hover',
            borderRadius: 1,
          }}
          data-testid="reply-preview"
        >
          <Typography
            variant="caption"
            sx={{ flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}
          >
            {zh.chat.reply} · {replyPreview.senderName}: {replyPreview.content}
          </Typography>
          <IconButton size="small" aria-label={zh.chat.cancel} onClick={onCancelReply} data-testid="cancel-reply">
            <CloseIcon sx={{ fontSize: 14 }} />
          </IconButton>
        </Box>
      )}
      <TextField
        value={text}
        onChange={(e) => {
          setText(e.target.value);
          onTyping?.();
        }}
        onKeyDown={onKeyDown}
        placeholder={zh.chat.sendHint}
        fullWidth
        multiline
        maxRows={4}
        size="small"
        disabled={disabled}
        data-testid="message-input"
        InputProps={{
          endAdornment: (
            <InputAdornment position="end">
              <IconButton onClick={submit} disabled={disabled || text.trim() === ''} aria-label="send">
                <SendIcon />
              </IconButton>
            </InputAdornment>
          ),
        }}
      />
    </Box>
  );
}
