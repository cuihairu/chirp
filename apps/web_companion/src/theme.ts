import { createTheme } from '@mui/material/styles';

// Dark, chat-density theme shared by every page.
export const theme = createTheme({
  palette: {
    mode: 'dark',
    primary: { main: '#6366f1' },
    background: { default: '#0f1117', paper: '#171a23' },
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
