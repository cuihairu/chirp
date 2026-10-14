import { useEffect } from 'react';
import { Box, Typography } from '@mui/material';

export interface MarqueeStripProps {
  content: string;
  ttlSeconds: number;
  onExpire: () => void;
}

/**
 * 走马灯（P0 消息展示）：PRIORITY_URGENT + ttl_seconds > 0 的消息在窗口顶部
 * 滚动，同时只显示一条，新的替换旧的（父层单槽 state 直接换 props）；到点
 * 自动收起。
 */
export default function MarqueeStrip({ content, ttlSeconds, onExpire }: MarqueeStripProps) {
  useEffect(() => {
    const timer = setTimeout(onExpire, ttlSeconds * 1000);
    return () => clearTimeout(timer);
  }, [content, ttlSeconds, onExpire]);
  return (
    <Box
      data-testid="marquee-strip"
      sx={{ py: 1, bgcolor: 'warning.light', color: 'text.primary', overflow: 'hidden' }}
    >
      <Typography
        variant="body2"
        sx={{
          fontWeight: 600,
          whiteSpace: 'nowrap',
          display: 'inline-block',
          pl: '100%',
          animation: 'chirp-marquee 18s linear infinite',
          '@keyframes chirp-marquee': {
            '0%': { transform: 'translateX(0)' },
            '100%': { transform: 'translateX(-100%)' },
          },
        }}
      >
        {content}
      </Typography>
    </Box>
  );
}