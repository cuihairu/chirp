/**
 * All user-facing copy in one place. Errors that map to server codes go
 * through protocol/errors errorText instead.
 */
export const zh = {
  appName: 'Chirp 伴侣',
  login: {
    title: '登录到 Chirp',
    subtitle: '开发模式:输入用户 ID 即可登录',
    userIdLabel: '用户 ID',
    submit: '登录',
    submitting: '登录中…',
  },
  banner: {
    reconnecting: '连接已断开,正在自动重连…',
    kicked: '账号已在其他设备登录,当前会话已断开',
    backToLogin: '返回登录',
  },
  chat: {
    welcome: (userId: string) => `欢迎,${userId}`,
    placeholder: '会话列表与聊天窗口将在后续版本提供。',
    signOut: '退出登录',
  },
} as const;
