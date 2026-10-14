import {
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  DialogTitle,
} from '@mui/material';
import { zh } from '../i18n/zh';

export interface SystemAnnounceDialogProps {
  content: string;
  onClose: () => void;
}

/**
 * 系统弹窗（P0 消息展示）：PRIORITY_HIGH 的系统消息（维护通知、全服奖励）
 * 弹窗展示，确认后关闭；消息本体仍按普通气泡留在会话流里。
 */
export default function SystemAnnounceDialog({ content, onClose }: SystemAnnounceDialogProps) {
  return (
    <Dialog open onClose={onClose} data-testid="system-announce-dialog">
      <DialogTitle>{zh.chat.announceTitle}</DialogTitle>
      <DialogContent>
        <DialogContentText sx={{ whiteSpace: 'pre-wrap' }}>{content}</DialogContentText>
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose} data-testid="announce-ack" autoFocus>
          {zh.chat.announceAck}
        </Button>
      </DialogActions>
    </Dialog>
  );
}
