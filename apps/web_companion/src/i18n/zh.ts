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
    newChat: '新的私聊',
    newChatTitle: '发起私聊',
    newChatLabel: '对方用户 ID',
    confirm: '开始聊天',
    cancel: '取消',
    emptyChannel: '选择左侧会话,或发起新的私聊。',
    loadEarlier: '加载更早的消息',
    loading: '加载中…',
    you: '我',
    sendHint: '输入消息,Enter 发送',
    statusPending: '发送中…',
    statusFailed: '发送失败',
    statusQueued: '对方离线,已排队',
    edited: '(已编辑)',
    deleted: '消息已撤回',
    startChatSelf: '不能和自己私聊',
    read: '已读',
    typing: (user: string) => `${user} 正在输入…`,
    addReaction: '添加表情',
    edit: '编辑',
    delete: '删除',
    editTitle: '编辑消息',
    save: '保存',
    deleteConfirm: '撤回这条消息?',
  },
} as const;
