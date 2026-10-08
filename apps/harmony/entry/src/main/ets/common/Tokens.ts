/**
 * 设计 token 单一事实源：从 design/prototypes/mobile/chirp.css 的 CSS 变量
 * 表逐项映射（近黑底 + indigo 强调 + 胶囊按钮 + 圆形头像）。ArkUI 无 CSS
 * 自定义属性，这里以常量收敛，页面只引用本表，不再散写字面色值。
 */

export const C = {
  /** 页面底色 */
  bg: '#0f1115',
  /** 卡片/列表行底 */
  surface: '#161923',
  /** 输入框、次级面 */
  surface2: '#1c2030',
  /** 悬浮/选中面 */
  surface3: '#232840',
  border: '#2a3046',
  borderSoft: '#222741',
  text: '#f3f4f6',
  text2: '#9ca3af',
  text3: '#6b7280',
  accent: '#6366f1',
  accentStrong: '#4f46e5',
  /** 强调文字（气泡内的链接、按钮文案） */
  accentSoft: '#a5b4fc',
  /** 强调弱化底（徽标、选中 chip） */
  accentDim: 'rgba(99,102,241,0.14)',
  online: '#34d399',
  danger: '#f87171',
  warn: '#fbbf24',
} as const;

/** 圆角：卡片 16 / 行与输入 12 / 胶囊 999 */
export const R = {
  card: 16,
  row: 12,
  pill: 999,
} as const;

/** 尺寸：顶栏 56 / 底部页签 64 / 头像 三档 */
export const S = {
  topbar: 56,
  tabbar: 64,
  avatarLg: 72,
  avatarMd: 44,
  avatarSm: 32,
} as const;

/** 与 apps/web_companion quick_reactions.ts 同源同一份顺序。 */
export const QUICK_REACTIONS: string[] = ['👍', '❤️', '😂', '😮', '😢', '😡', '🎉', '👀'];

/**
 * 头像底色：按名字 hash 稳定取色（同 web 端 hue-hashed 头像的观感；
 * ArkUI 无 hsl() 字符串支持面，这里折算成 hex）。
 */
export function avatarColor(name: string): string {
  let hash = 0;
  for (let i = 0; i < name.length; i++) {
    hash = (hash * 31 + name.charCodeAt(i)) >>> 0;
  }
  const hue = hash % 360;
  return hslToHex(hue, 0.42, 0.46);
}

function hslToHex(h: number, s: number, l: number): string {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const hp = h / 60;
  const x = c * (1 - Math.abs((hp % 2) - 1));
  let rgb: number[];
  if (hp < 1) rgb = [c, x, 0];
  else if (hp < 2) rgb = [x, c, 0];
  else if (hp < 3) rgb = [0, c, x];
  else if (hp < 4) rgb = [0, x, c];
  else if (hp < 5) rgb = [x, 0, c];
  else rgb = [c, 0, x];
  const m = l - c / 2;
  const hex = rgb
    .map((v) => Math.round((v + m) * 255)
      .toString(16)
      .padStart(2, '0'));
  return `#${hex[0]}${hex[1]}${hex[2]}`;
}

/** 名字首字符（头像占位）；空名回落 '？'。 */
export function avatarInitial(name: string): string {
  return name.length > 0 ? name.charAt(0).toUpperCase() : '？';
}

/** HH:MM（本地时区）；历史时间戳的列表/气泡内联展示用。 */
export function hhmm(ts: number): string {
  const d = new Date(ts);
  const p = (n: number): string => (n < 10 ? `0${n}` : `${n}`);
  return `${p(d.getHours())}:${p(d.getMinutes())}`;
}

/**
 * 会话列表尾时间：今天显示 HH:MM，更早显示 MM-DD。
 * （web 端用 MUI 相对时间；移动端收敛为两态，避免第三方依赖。）
 */
export function conversationTime(ts: number | undefined): string {
  if (!ts) return '';
  const d = new Date(ts);
  const now = new Date();
  if (d.getFullYear() === now.getFullYear() && d.getMonth() === now.getMonth() && d.getDate() === now.getDate()) {
    return hhmm(ts);
  }
  const p = (n: number): string => (n < 10 ? `0${n}` : `${n}`);
  return `${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

/** 路由参数容器：router.pushUrl 的 params 传类实例，getParams 还原。 */
export class ChatRouteParams {
  /** 会话 key（'p:a|b' / 'g:<id>'）。 */
  key: string = '';
}

export class GroupRouteParams {
  /** 群组 id；空串 = 列表/新建模式。 */
  groupId: string = '';
}
