// P1 表情/贴图：系统表情包（内置 16 个）。消息文本中的 [emoji:code] 展开为
// 对应 emoji 字符渲染；未知 code 原样保留（向前兼容，与 sender_kind/品质/
// 稀有度回落同口径）。输入面经选择器插入表情码，服务端按 TEXT 透传
// （内容面零改动，长度/敏感词门照常生效）。

export interface SystemEmoji {
  code: string;
  emoji: string;
}

export const SYSTEM_EMOJIS: SystemEmoji[] = [
  { code: 'smile', emoji: '😄' },
  { code: 'laugh', emoji: '😂' },
  { code: 'heart', emoji: '❤️' },
  { code: 'thumbsup', emoji: '👍' },
  { code: 'thumbsdown', emoji: '👎' },
  { code: 'clap', emoji: '👏' },
  { code: 'wave', emoji: '👋' },
  { code: 'ok', emoji: '🆗' },
  { code: 'cry', emoji: '😢' },
  { code: 'angry', emoji: '😠' },
  { code: 'surprise', emoji: '😲' },
  { code: 'cool', emoji: '😎' },
  { code: 'thinking', emoji: '🤔' },
  { code: 'fire', emoji: '🔥' },
  { code: 'star', emoji: '⭐' },
  { code: 'party', emoji: '🎉' },
];

const EMOJI_BY_CODE = new Map(SYSTEM_EMOJIS.map((e) => [e.code, e.emoji]));

const EMOJI_CODE_RE = /\[emoji:([a-z0-9_]+)\]/g;

export type TextSegment = { kind: 'text'; value: string } | { kind: 'emoji'; code: string; emoji: string };

/** Split message text into text / emoji-code segments; unknown codes stay text. */
export const expandEmojiCodes = (text: string): TextSegment[] => {
  const segments: TextSegment[] = [];
  let last = 0;
  for (const match of text.matchAll(EMOJI_CODE_RE)) {
    const emoji = EMOJI_BY_CODE.get(match[1]);
    if (!emoji) continue;
    const start = match.index ?? 0;
    if (start > last) segments.push({ kind: 'text', value: text.slice(last, start) });
    segments.push({ kind: 'emoji', code: match[1], emoji });
    last = start + match[0].length;
  }
  if (last < text.length) segments.push({ kind: 'text', value: text.slice(last) });
  return segments;
};
