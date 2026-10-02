import { SendMessageRequest } from '@chirp/proto/chat';
import { describe, expect, it } from 'vitest';
import { WordFilterInterceptor, parseWordLexicon } from './word_filter';

function encode(content: string): Uint8Array {
  return new TextEncoder().encode(content);
}

function run(filter: WordFilterInterceptor, content: string): { allowed: boolean; text: string } {
  const request = SendMessageRequest.fromPartial({ content: encode(content) });
  const allowed = filter.onBeforeSend(request);
  return { allowed, text: new TextDecoder().decode(request.content) };
}

describe('parseWordLexicon', () => {
  it('trims, drops blanks and comments, lower-cases and de-dupes', () => {
    expect(parseWordLexicon([
      '  Spam  ',
      '# 注释行',
      '',
      'spam',
      '  dummy\r\n',
      '论坛',
    ])).toEqual(['dummy', 'spam', '论坛']);
  });
});

describe('WordFilterInterceptor', () => {
  it('replace masks hits and keeps surrounding casing', () => {
    const filter = new WordFilterInterceptor({ terms: ['bad dog'] });
    expect(run(filter, 'Bad DOG and bad dog')).toEqual({ allowed: true, text: '** and **' });
    expect(filter.wordCount).toBe(1);
  });

  it('replace collapses adjacent term runs into one replacement', () => {
    const filter = new WordFilterInterceptor({ terms: ['ab', 'bc'], replacement: '#' });
    expect(run(filter, 'abc')).toEqual({ allowed: true, text: '#' });
  });

  it('replace keeps utf-8 bytes around ascii hits', () => {
    const filter = new WordFilterInterceptor({ terms: ['脏话', 'damn'] });
    expect(run(filter, '你好 damn 世界,真是脏话啊'))
      .toEqual({ allowed: true, text: '你好 ** 世界,真是**啊' });
  });

  it('reject blocks hits and passes clean text untouched', () => {
    const filter = new WordFilterInterceptor({ terms: ['banned'], policy: 'reject' });
    expect(run(filter, 'totally BANNED words')).toEqual({ allowed: false, text: 'totally BANNED words' });
    expect(run(filter, 'perfectly fine')).toEqual({ allowed: true, text: 'perfectly fine' });
  });

  it('empty lexicon is a no-op', () => {
    const filter = new WordFilterInterceptor({ terms: [] });
    expect(run(filter, 'anything at all')).toEqual({ allowed: true, text: 'anything at all' });
  });
});
