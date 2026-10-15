// P1 富文本（game_chat_features P1 消息处理）：消息文本里的 **粗体** 与
// *斜体* 标记在气泡内渲染为 strong/em，服务端按 TEXT 透传（内容面零改动，
// 长度/敏感词门照常生效，与 P1 表情批同口径）。
//
// 解析刻意保守：定界符左侧必须不是空白（串首算合法边界），跨度内必须非空
// 且成对闭合——否则原样保留字面。于是 "2 * 3 = 4"、"user_id" 这类正文里的裸
// 星号/下划线不会被吞掉，未闭合的 "**粗" 也照原样显示；右定界符后接空白不
// 影响（中文正文里标记紧贴前字更常见）。
//
// 颜色（VIP 特权）不在本批：聊天面无 VIP 权益数据源（与聊天等级门槛同款
// 缺口），等权益面出现再随其一起落。

export interface RichRun {
  text: string;
  bold: boolean;
  italic: boolean;
}

/** Opening markers, longest first so `**` wins over `*`. */
const MARKERS: Array<{ marker: string; bold: boolean; italic: boolean }> = [
  { marker: '**', bold: true, italic: false },
  { marker: '*', bold: false, italic: true },
];

/**
 * Split message text into styled runs. Unmatched markers stay literal: a
 * marker pair only forms a run when both sides sit on non-blitespace
 * boundaries and the span between them carries content.
 */
export const parseRichText = (text: string): RichRun[] => {
  const runs: RichRun[] = [];
  let buffer = '';
  let i = 0;

  const flush = (): void => {
    if (buffer) {
      runs.push({ text: buffer, bold: false, italic: false });
      buffer = '';
    }
  };

  while (i < text.length) {
    const marker = MARKERS.find((m) => text.startsWith(m.marker, i));
    if (!marker) {
      buffer += text[i];
      i += 1;
      continue;
    }
    const contentStart = i + marker.marker.length;
    const close = findClose(text, contentStart, marker.marker);
    if (close < 0 || !onBoundary(text, i - 1)) {
      // No closer with real content, or the marker hugs whitespace: it is
      // ordinary text (math, snake_case, a half-typed marker).
      buffer += marker.marker;
      i = contentStart;
      continue;
    }
    flush();
    runs.push({ text: text.slice(contentStart, close), bold: marker.bold, italic: marker.italic });
    i = close + marker.marker.length;
  }
  flush();
  return runs.length > 0 ? runs : [{ text, bold: false, italic: false }];
};

/**
 * Index of the closer for `marker` starting at `from`, or -1. The closer counts
 * only when the span it closes carries non-blank content — the "not glued to
 * whitespace" guard lives on the opening side, where it is what keeps
 * arithmetic like "2 * 3 * 4" out of the formatter.
 */
const findClose = (text: string, from: number, marker: string): number => {
  for (let i = from; i + marker.length <= text.length; i++) {
    if (!text.startsWith(marker, i)) continue;
    if (text.slice(from, i).trim() === '') continue;
    return i;
  }
  return -1;
};

/** A marker edge is valid at the string ends or next to non-whitespace. */
const onBoundary = (text: string, index: number): boolean => {
  if (index < 0 || index >= text.length) return true;
  return !/\s/.test(text[index]);
};