import { useState } from 'react';
import type { KeyboardEvent } from 'react';
import { IconButton, InputAdornment, TextField } from '@mui/material';
import SendIcon from '@mui/icons-material/Send';
import { zh } from '../i18n/zh';

/** Message composer: Enter sends, Shift+Enter breaks a line. */
export default function MessageInput({
  onSend,
  onTyping,
  disabled = false,
}: {
  onSend: (text: string) => void;
  /** Fired on every keystroke; the parent owns the typing throttle. */
  onTyping?: () => void;
  disabled?: boolean;
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
  );
}
