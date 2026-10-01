import { createTheme } from '@mui/material/styles';

// 桌面聊天 App 深色主题：与图标同族的青绿主色。
export const theme = createTheme({
  palette: {
    mode: 'dark',
    primary: { main: '#2ecc9f' },
    secondary: { main: '#7c8cf8' },
    background: { default: '#181a20', paper: '#1f222a' },
    divider: '#2c303a',
  },
  shape: { borderRadius: 10 },
  typography: {
    fontFamily: '"Noto Sans CJK SC", "PingFang SC", "Microsoft YaHei", system-ui, sans-serif',
  },
  components: {
    MuiListItemButton: {
      defaultProps: { dense: true },
    },
  },
});
