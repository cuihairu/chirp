import { createTheme } from '@mui/material/styles';

// Dark, chat-density theme shared by every page.
export const theme = createTheme({
  palette: {
    mode: 'dark',
    primary: { main: '#6366f1' },
    background: { default: '#0f1117', paper: '#171a23' },
    // 显式钉住三级文本层级：MUI 9 的 Typography color prop 只认驼峰写法
    // （textSecondary/textDisabled），点号写法（color="text.secondary"）静默
    // 通过类型检查但运行时不生成任何颜色类，全部回落继承 text.primary——
    // 时间戳、空态文案与脚注跟正文同亮。值与桌面端同款（桌面端 UI 审核实测
    // 14.03/7.55/4.56，均 ≥AA）；sx 里的 color: 'text.primary' 走 sx 引擎，
    // 点号写法本来就好，不受影响。
    text: {
      primary: '#eef1f4',
      secondary: 'rgba(238, 241, 244, 0.70)',
      disabled: 'rgba(238, 241, 244, 0.50)',
    },
  },
  typography: {
    fontSize: 13.5,
  },
  components: {
    MuiDrawer: {
      styleOverrides: {
        paper: { backgroundColor: '#171a23' },
      },
    },
  },
});
