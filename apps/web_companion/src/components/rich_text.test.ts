import { describe, expect, it } from 'vitest';
import { parseRichText } from './rich_text';

describe('parseRichText', () => {
  it('splits a bold span into its own run', () => {
    expect(parseRichText('胜利!**一波带走**冲!')).toEqual([
      { text: '胜利!', bold: false, italic: false },
      { text: '一波带走', bold: true, italic: false },
      { text: '冲!', bold: false, italic: false },
    ]);
  });

  it('splits an italic span into its own run', () => {
    expect(parseRichText('这波*真的秀*了')).toEqual([
      { text: '这波', bold: false, italic: false },
      { text: '真的秀', bold: false, italic: true },
      { text: '了', bold: false, italic: false },
    ]);
  });

  it('keeps plain text as a single unstyled run', () => {
    expect(parseRichText('没有标记的消息')).toEqual([
      { text: '没有标记的消息', bold: false, italic: false },
    ]);
  });

  it('returns an unstyled run for empty text', () => {
    expect(parseRichText('')).toEqual([{ text: '', bold: false, italic: false }]);
  });

  it('leaves an unclosed marker literal', () => {
    expect(parseRichText('**没闭合')).toEqual([{ text: '**没闭合', bold: false, italic: false }]);
  });

  it('leaves an empty span literal', () => {
    expect(parseRichText('a****b')).toEqual([{ text: 'a****b', bold: false, italic: false }]);
  });

  it('does not eat math-style asterisks surrounded by spaces', () => {
    expect(parseRichText('2 * 3 * 4 = 24')).toEqual([
      { text: '2 * 3 * 4 = 24', bold: false, italic: false },
    ]);
  });

  it('does not eat a marker hugging whitespace', () => {
    expect(parseRichText('看这个 ** 重点')).toEqual([
      { text: '看这个 ** 重点', bold: false, italic: false },
    ]);
  });

  it('reads consecutive spans', () => {
    expect(parseRichText('**粗**夹*斜*')).toEqual([
      { text: '粗', bold: true, italic: false },
      { text: '夹', bold: false, italic: false },
      { text: '斜', bold: false, italic: true },
    ]);
  });

  it('keeps whitespace inside a span', () => {
    expect(parseRichText('**粗 体**')).toEqual([{ text: '粗 体', bold: true, italic: false }]);
  });
});