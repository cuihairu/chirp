import 'dart:convert';
import 'dart:typed_data';

import 'package:chirp_proto/proto/chat.pb.dart' as chat;

import 'hooks.dart';

/// 敏感词命中的两种客户端处置。没有 record:记录待审核是服务端职责
/// (服务端 WordFilter 已实现三级策略),客户端预检的职责只是不让脏词
/// 出门——要么改写,要么拦截。
enum WordFilterPolicy { replace, reject }

class WordFilterOptions {
  WordFilterOptions({
    this.terms = const <String>[],
    this.policy = WordFilterPolicy.replace,
    this.replacement = '**',
  });

  /// 词集合,大小写不敏感;构造时统一 lower + 去重。
  final List<String> terms;
  final WordFilterPolicy policy;
  final String replacement;
}

/// 敏感词预检拦截器:行为对齐服务端 chirp::chat::WordFilter 与 C++ core
/// (word_filter.h)/C#(WordFilterInterceptor.cs)的同名实现——词库格式、
/// ASCII 大小写不敏感子串匹配、mask 后重建的替换语义。只滤发送侧:接收
/// 内容信任服务端已按其策略处理。实例构造后不可变,可并发复用。
class WordFilterInterceptor extends MessageInterceptor {
  WordFilterInterceptor(WordFilterOptions options)
      : terms = parseWordLexicon(options.terms),
        _policy = options.policy,
        _replacement = options.replacement;

  /// lower-cased, deduplicated, sorted。
  final List<String> terms;
  final WordFilterPolicy _policy;
  final String _replacement;

  int get wordCount => terms.length;

  /// replace:命中时把改写后的内容写回 request.content 并放行;无命中
  /// 原样放行。reject:命中返回 false(管线抛 RequestError(blocked))。
  @override
  bool onBeforeSend(chat.SendMessageRequest request) {
    final content = utf8.decode(request.content);
    final lowered = asciiLower(content);
    final masked = _maskHits(lowered);
    if (masked == null) {
      return true;
    }
    if (_policy == WordFilterPolicy.reject) {
      return false;
    }
    request.content =
        Uint8List.fromList(utf8.encode(_rebuild(content, lowered, masked)));
    return true;
  }

  /// null = 无命中;否则为按 code unit 的命中区间 mask。
  List<bool>? _maskHits(String lowered) {
    if (terms.isEmpty) return null;
    final masked = List<bool>.filled(lowered.length, false);
    var hit = false;
    for (final term in terms) {
      // 词永不为空:parseWordLexicon 丢掉空行。
      var pos = lowered.indexOf(term);
      while (pos >= 0) {
        hit = true;
        for (var i = pos; i < pos + term.length; i++) {
          masked[i] = true;
        }
        pos = lowered.indexOf(term, pos + term.length);
      }
    }
    return hit ? masked : null;
  }

  /// 连续命中塌缩成一次替换,未命中字符保留原大小写。
  String _rebuild(String content, String lowered, List<bool> masked) {
    final filtered = StringBuffer();
    var i = 0;
    while (i < lowered.length) {
      if (masked[i]) {
        filtered.write(_replacement);
        while (i < lowered.length && masked[i]) {
          i++;
        }
      } else {
        filtered.writeCharCode(content.codeUnitAt(i));
        i++;
      }
    }
    return filtered.toString();
  }
}

/// 按服务端词库文件格式(每行一词,# 开头为注释,空行忽略)逐行解析。
/// trim 集合与服务端一致:空格与 CR/LF,不含 tab 等其余空白。
List<String> parseWordLexicon(List<String> lines) {
  final unique = <String>{};
  for (final raw in lines) {
    var term = raw;
    while (term.isNotEmpty &&
        (term.endsWith(' ') || term.endsWith('\r') || term.endsWith('\n'))) {
      term = term.substring(0, term.length - 1);
    }
    while (term.startsWith(' ')) {
      term = term.substring(1);
    }
    if (term.isEmpty || term.startsWith('#')) {
      continue;
    }
    unique.add(asciiLower(term));
  }
  final sorted = unique.toList()..sort();
  return sorted;
}

/// 只折叠 ASCII 大小写,与服务端 ToLower(默认 C locale)逐字节一致;
/// UTF-8 多字节序列不受影响,按 code unit(BMP 内与字节序一致)匹配。
String asciiLower(String text) {
  final out = StringBuffer();
  for (var i = 0; i < text.length; i++) {
    final c = text.codeUnitAt(i);
    out.writeCharCode(c >= 0x41 && c <= 0x5a ? c + 0x20 : c);
  }
  return out.toString();
}
