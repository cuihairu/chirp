import { createTheme } from '@mui/material/styles';

// 桌面聊天 App 深色主题：与图标同族的青绿主色。
export const theme = createTheme({
  palette: {
    mode: 'dark',
    primary: { main: '#2ecc9f' },
    secondary: { main: '#7c8cf8' },
    background: { default: '#181a20', paper: '#1f222a' },
    divider: '#2c303a',
    // 显式钉住三级文本层级：MUI 9 的 Typography color 只认 textSecondary/
    // textDisabled 驼峰写法，点号写法（color="text.secondary"）静默通过类型
    // 检查但运行时不生成任何颜色类，全部回落继承 text.primary——时间戳、
    // 左栏脚注、气泡发送者名与正文同亮。0.5/0.7 两档在 paper 上仍 ≥5:1。
    text: {
      primary: '#eef1f4',
      secondary: 'rgba(238, 241, 244, 0.70)',
      disabled: 'rgba(238, 241, 244, 0.50)',
    },
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
