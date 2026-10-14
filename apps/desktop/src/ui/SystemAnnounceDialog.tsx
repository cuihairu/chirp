import {
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  DialogTitle,
} from '@mui/material';

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
      <DialogTitle>系统公告</DialogTitle>
      <DialogContent>
        <DialogContentText sx={{ whiteSpace: 'pre-wrap' }}>{content}</DialogContentText>
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose} data-testid="announce-ack" autoFocus>
          知道了
        </Button>
      </DialogActions>
    </Dialog>
  );
}