/**
 * Sensitive-word pre-check interceptor — TypeScript face of the same
 * implementation the C++ core ships (sdks/core/include/chirp/word_filter.h)
 * and the Unity SDK (Runtime/Chirp/WordFilterInterceptor.cs), aligned with
 * the server-side chirp::chat::WordFilter: lexicon format, ASCII
 * case-insensitive substring matching, and the mask-then-rebuild replace
 * semantics. Send side only — receive content is trusted to the server's
 * own policy.
 */
import type { SendMessageRequest } from '@chirp/proto/chat';
import type { MessageInterceptor } from './hooks';

export type WordFilterPolicy = 'replace' | 'reject';

export interface WordFilterOptions {
  /** Terms, case-insensitive; lower-cased and de-duplicated on construction. */
  terms: readonly string[];
  /** Default 'replace'. There is no 'record': audit is the server's job. */
  policy?: WordFilterPolicy;
  /** Mask for replaced runs; default '**'. */
  replacement?: string;
}

/** Lexicon lines in the server file format (one term per line, '#' starts a
 * comment line, blank lines ignored, terms lower-cased and de-duplicated).
 * Feed the same lexicon file the server loads and both ends agree. */
export function parseWordLexicon(lines: readonly string[]): string[] {
  const unique = new Set<string>();
  for (const raw of lines) {
    // Trim exactly what the server trims: spaces and CR/LF — not the full
    // String.prototype.trim whitespace set.
    const term = raw.replace(/^[ ]+/, '').replace(/[ \r\n]+$/, '');
    if (term.length === 0 || term.startsWith('#')) {
      continue;
    }
    unique.add(asciiLower(term));
  }
  return [...unique].sort();
}

/** ASCII-only case folding, matching the server's ToLower (C locale,
 * byte-wise). UTF-8 multi-byte sequences pass through untouched. */
function asciiLower(text: string): string {
  let out = '';
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    out += code >= 0x41 && code <= 0x5a ? String.fromCharCode(code + 0x20) : text[i];
  }
  return out;
}

export class WordFilterInterceptor implements MessageInterceptor {
  readonly wordCount: number;

  private readonly terms: string[]; // lower-cased, deduplicated, sorted
  private readonly policy: WordFilterPolicy;
  private readonly replacement: string;

  constructor(options: WordFilterOptions) {
    this.terms = parseWordLexicon(options.terms);
    this.policy = options.policy ?? 'replace';
    this.replacement = options.replacement ?? '**';
    this.wordCount = this.terms.length;
  }

  /** 'replace': hits are rewritten into req.content and the send proceeds;
   * clean text passes untouched. 'reject': a hit returns false — the
   * pipeline throws RequestError('blocked') and nothing reaches the wire. */
  onBeforeSend(request: SendMessageRequest): boolean {
    const content = new TextDecoder().decode(request.content);
    const lowered = asciiLower(content);
    const masked = this.maskHits(lowered);
    if (masked === null) {
      return true;
    }
    if (this.policy === 'reject') {
      return false;
    }
    request.content = new TextEncoder().encode(this.rebuild(content, lowered, masked));
    return true;
  }

  /** null = no hit; boolean[] = per-code-unit mask over the hit ranges. */
  private maskHits(lowered: string): boolean[] | null {
    if (this.terms.length === 0) {
      return null;
    }
    const masked = new Array<boolean>(lowered.length).fill(false);
    let hit = false;
    for (const term of this.terms) {
      // Terms are never empty: parseWordLexicon drops blank lines.
      let pos = lowered.indexOf(term);
      while (pos >= 0) {
        hit = true;
        masked.fill(true, pos, pos + term.length);
        pos = lowered.indexOf(term, pos + term.length);
      }
    }
    return hit ? masked : null;
  }

  /** Rebuild with adjacent hits collapsed into one replacement; characters
   * outside hit ranges keep their original casing. */
  private rebuild(content: string, lowered: string, masked: boolean[]): string {
    let filtered = '';
    for (let i = 0; i < lowered.length;) {
      if (masked[i]) {
        filtered += this.replacement;
        while (i < lowered.length && masked[i]) {
          i++;
        }
      } else {
        filtered += content[i];
        i++;
      }
    }
    return filtered;
  }
}
