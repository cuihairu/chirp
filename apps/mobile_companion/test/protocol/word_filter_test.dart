import 'dart:convert';

import 'package:chirp_proto/proto/chat.pb.dart' as chat;
import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/protocol/word_filter.dart';

void main() {
  group('parseWordLexicon', () {
    test('trims, drops blanks and comments, lower-cases and de-dupes', () {
      expect(
        parseWordLexicon([
          '  Spam  ',
          '# 注释行',
          '',
          'spam',
          '  dummy\r\n',
          '论坛',
        ]),
        ['dummy', 'spam', '论坛'],
      );
    });
  });

  group('WordFilterInterceptor', () {
    ({bool allowed, String text}) run(
        WordFilterInterceptor filter, String content) {
      final request = chat.SendMessageRequest(content: utf8.encode(content));
      final allowed = filter.onBeforeSend(request);
      return (allowed: allowed, text: utf8.decode(request.content));
    }

    test('replace masks hits and keeps surrounding casing', () {
      final filter =
          WordFilterInterceptor(WordFilterOptions(terms: ['bad dog']));
      expect(run(filter, 'Bad DOG and bad dog'),
          (allowed: true, text: '** and **'));
      expect(filter.wordCount, 1);
    });

    test('replace collapses adjacent term runs into one replacement', () {
      final filter = WordFilterInterceptor(
          WordFilterOptions(terms: ['ab', 'bc'], replacement: '#'));
      expect(run(filter, 'abc'), (allowed: true, text: '#'));
    });

    test('replace keeps utf-8 bytes around ascii hits', () {
      final filter =
          WordFilterInterceptor(WordFilterOptions(terms: ['脏话', 'damn']));
      expect(run(filter, '你好 damn 世界,真是脏话啊'),
          (allowed: true, text: '你好 ** 世界,真是**啊'));
    });

    test('reject blocks hits and passes clean text untouched', () {
      final filter = WordFilterInterceptor(WordFilterOptions(
          terms: ['banned'], policy: WordFilterPolicy.reject));
      expect(run(filter, 'totally BANNED words'),
          (allowed: false, text: 'totally BANNED words'));
      expect(run(filter, 'perfectly fine'),
          (allowed: true, text: 'perfectly fine'));
    });

    test('empty lexicon is a no-op', () {
      final filter = WordFilterInterceptor(WordFilterOptions());
      expect(run(filter, 'anything at all'),
          (allowed: true, text: 'anything at all'));
    });
  });
}
