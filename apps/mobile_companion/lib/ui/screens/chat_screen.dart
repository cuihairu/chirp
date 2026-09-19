import 'dart:async';

import 'package:flutter/material.dart';

import '../../api/chat_api.dart';
import '../../api/services.dart';
import '../../state/conversation_store.dart';
import '../../state/message_store.dart';
import '../../state/models.dart';
import '../../state/typing_presence.dart';

/// One conversation: history with upward pagination, optimistic sends,
/// reactions/edit/delete via long-press, typing indicator, and read receipts
/// (private channels). Opening it marks the channel active — the root uses
/// that to suppress notifications — and clears the unread badge.
class ChatScreen extends StatefulWidget {
  const ChatScreen({
    super.key,
    required this.services,
    required this.conversation,
  });

  final Services services;
  final Conversation conversation;

  @override
  State<ChatScreen> createState() => _ChatScreenState();
}

class _ChatScreenState extends State<ChatScreen> {
  static const _reactions = ['👍', '❤️', '😂', '😮', '😢'];

  late final String _selfId;
  late final ChannelRef _channel;
  late final TextEditingController _input;
  final ScrollController _scroll = ScrollController();
  Timer? _typingTicker;
  Timer? _typingStop;
  bool _typingSent = false;
  int _lastTypingSentAt = 0;
  bool _hadTyping = false;
  String? _lastReadMark;
  bool _loadingOlder = false;

  @override
  void initState() {
    super.initState();
    _selfId = widget.services.auth.value.userId ?? '';
    _channel = channelRefOf(widget.conversation.key, _selfId);
    _input = TextEditingController();
    _scroll.addListener(_onScroll);
    widget.services.messages.addListener(_onMessagesChanged);
    widget.services.api.setActiveChannel(_channel.key);
    clearUnread(widget.services.conversations, _channel.key);
    unawaited(_initialLoad());
    _typingTicker = Timer.periodic(
      const Duration(seconds: 3),
      (_) => _onTypingTick(),
    );
  }

  @override
  void dispose() {
    _sendTypingStop();
    _typingTicker?.cancel();
    _typingStop?.cancel();
    widget.services.messages.removeListener(_onMessagesChanged);
    widget.services.api.setActiveChannel(null);
    _input.dispose();
    _scroll.dispose();
    super.dispose();
  }

  Future<void> _initialLoad() async {
    await widget.services.api.loadHistory(_channel);
    await _markLatestRead();
  }

  void _onMessagesChanged() {
    unawaited(_markLatestRead());
  }

  /// Peer progress receipts: send the newest confirmed message id.
  Future<void> _markLatestRead() async {
    final list = messagesOf(widget.services.messages.value, _channel.key);
    for (final message in list.reversed) {
      if (message.pending) continue;
      if (_lastReadMark == message.messageId) return;
      _lastReadMark = message.messageId;
      await widget.services.api
          .markRead(_channel, messageId: message.messageId);
      return;
    }
  }

  void _onScroll() {
    if (_scroll.position.pixels >= _scroll.position.maxScrollExtent - 400) {
      unawaited(_loadOlder());
    }
  }

  Future<void> _loadOlder() async {
    if (_loadingOlder) return;
    final state = widget.services.messages.value;
    if (state.loadingHistory[_channel.key] == true) return;
    if (state.hasMore[_channel.key] == false) return;
    final list = messagesOf(state, _channel.key);
    if (list.isEmpty) return;
    _loadingOlder = true;
    try {
      await widget.services.api
          .loadHistory(_channel, beforeTimestamp: list.first.timestamp);
    } finally {
      _loadingOlder = false;
    }
  }

  void _onTypingTick() {
    if (!mounted) return;
    final typing = typingUsersOf(
      widget.services.typing.value,
      _channel.key,
      DateTime.now().millisecondsSinceEpoch,
    );
    if (typing.isNotEmpty || _hadTyping) {
      setState(() => _hadTyping = typing.isNotEmpty);
    }
  }

  void _onInputChanged(String text) {
    final now = DateTime.now().millisecondsSinceEpoch;
    if (text.isNotEmpty && !_typingSent && now - _lastTypingSentAt > 3000) {
      _typingSent = true;
      _lastTypingSentAt = now;
      widget.services.api.sendTyping(_channel, true);
      _typingStop?.cancel();
      _typingStop = Timer(const Duration(seconds: 5), _sendTypingStop);
    }
  }

  void _sendTypingStop() {
    if (!_typingSent) return;
    _typingSent = false;
    widget.services.api.sendTyping(_channel, false);
  }

  Future<void> _send() async {
    final text = _input.text.trim();
    if (text.isEmpty) return;
    _input.clear();
    _sendTypingStop();
    await widget.services.api.sendToChannel(_channel, text);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text(widget.conversation.title)),
      body: ListenableBuilder(
        listenable: Listenable.merge([
          widget.services.messages,
          widget.services.typing,
        ]),
        builder: (context, _) {
          final state = widget.services.messages.value;
          final list = messagesOf(state, _channel.key);
          final typingUsers = typingUsersOf(
            widget.services.typing.value,
            _channel.key,
            DateTime.now().millisecondsSinceEpoch,
          );
          return Column(
            children: [
              Expanded(
                child: list.isEmpty
                    ? const Center(child: Text('还没有消息,发第一条吧'))
                    : ListView.builder(
                        reverse: true,
                        controller: _scroll,
                        padding: const EdgeInsets.symmetric(
                            horizontal: 12, vertical: 8),
                        itemCount: list.length,
                        itemBuilder: (context, index) =>
                            _bubble(list[list.length - 1 - index], list, state),
                      ),
              ),
              if (typingUsers.isNotEmpty)
                Padding(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                  child: Align(
                    alignment: Alignment.centerLeft,
                    child: Text(
                      typingUsers.length == 1
                          ? '${typingUsers.first} 正在输入…'
                          : '${typingUsers.join('、')} 正在输入…',
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                  ),
                ),
              SafeArea(
                top: false,
                child: Padding(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                  child: Row(
                    children: [
                      Expanded(
                        child: TextField(
                          controller: _input,
                          onChanged: _onInputChanged,
                          onSubmitted: (_) => unawaited(_send()),
                          decoration: const InputDecoration(
                            hintText: '输入消息…',
                            border: OutlineInputBorder(),
                            isDense: true,
                            contentPadding: EdgeInsets.symmetric(
                                horizontal: 12, vertical: 10),
                          ),
                        ),
                      ),
                      IconButton(
                        tooltip: '发送',
                        icon: const Icon(Icons.send),
                        onPressed: () => unawaited(_send()),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }

  Widget _bubble(
    ChatMessageView message,
    List<ChatMessageView> list,
    MessageState state,
  ) {
    final own = message.senderId == _selfId;
    return Align(
      alignment: own ? Alignment.centerRight : Alignment.centerLeft,
      child: GestureDetector(
        onTap: message.failed
            ? () => unawaited(
                widget.services.api.sendToChannel(_channel, message.content))
            : null,
        onLongPress: () => _messageActions(message),
        child: Container(
          margin: const EdgeInsets.symmetric(vertical: 3),
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          constraints: BoxConstraints(
            maxWidth: MediaQuery.of(context).size.width * 0.75,
          ),
          decoration: BoxDecoration(
            color: message.failed
                ? Theme.of(context).colorScheme.errorContainer
                : own
                    ? Theme.of(context).colorScheme.primaryContainer
                    : Theme.of(context).colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(12),
          ),
          child: Column(
            crossAxisAlignment:
                own ? CrossAxisAlignment.end : CrossAxisAlignment.start,
            children: [
              if (!own && widget.conversation.kind == ConversationKind.group)
                Text(
                  message.senderId,
                  style: Theme.of(context).textTheme.labelSmall,
                ),
              if (message.deleted)
                Text(
                  '消息已删除',
                  style: Theme.of(context)
                      .textTheme
                      .bodyMedium
                      ?.copyWith(fontStyle: FontStyle.italic),
                )
              else
                SelectableText(message.content),
              const SizedBox(height: 2),
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  if (message.pending) ...[
                    const Icon(Icons.schedule, size: 12),
                    const SizedBox(width: 2),
                    const Text('发送中', style: TextStyle(fontSize: 11)),
                  ],
                  if (message.failed)
                    const Text('发送失败,点击重试', style: TextStyle(fontSize: 11)),
                  if (message.queuedOffline)
                    const Text('离线暂存', style: TextStyle(fontSize: 11)),
                  if (message.edited)
                    const Text('已编辑', style: TextStyle(fontSize: 11)),
                  Text(_timeOf(message.timestamp),
                      style: const TextStyle(fontSize: 11)),
                  if (_readReceipt(message, list, state) != null) ...[
                    const SizedBox(width: 4),
                    const Text('已读', style: TextStyle(fontSize: 11)),
                  ],
                ],
              ),
              if (message.reactions.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Wrap(
                    spacing: 4,
                    children: [
                      for (final reaction in message.reactions.values)
                        InputChip(
                          visualDensity: VisualDensity.compact,
                          label: Text('${reaction.emoji} ${reaction.count}'),
                          onPressed: () => unawaited(
                            reaction.mine
                                ? widget.services.api.removeReaction(
                                    message.messageId, reaction.emoji)
                                : widget.services.api.addReaction(
                                    message.messageId, reaction.emoji),
                          ),
                        ),
                    ],
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }

  String? _readReceipt(
    ChatMessageView message,
    List<ChatMessageView> list,
    MessageState state,
  ) {
    if (widget.conversation.kind != ConversationKind.private) return null;
    if (message.senderId != _selfId) return null;
    final cursor = readCursorOf(state, _channel.key, _channel.peerId);
    if (cursor == null) return null;
    final cursorIndex = list.indexWhere((m) => m.messageId == cursor);
    final myIndex = list.indexWhere((m) => m.messageId == message.messageId);
    if (cursorIndex < 0 || myIndex < 0) return null;
    return myIndex <= cursorIndex ? '已读' : null;
  }

  void _messageActions(ChatMessageView message) {
    if (message.deleted) return;
    final own = message.senderId == _selfId && !message.pending;
    showModalBottomSheet<void>(
      context: context,
      builder: (sheetContext) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 8),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  for (final emoji in _reactions)
                    TextButton(
                      onPressed: () {
                        Navigator.pop(sheetContext);
                        final mine = message.reactions[emoji]?.mine ?? false;
                        unawaited(
                          mine
                              ? widget.services.api
                                  .removeReaction(message.messageId, emoji)
                              : widget.services.api
                                  .addReaction(message.messageId, emoji),
                        );
                      },
                      child: Text(
                        emoji,
                        style: const TextStyle(fontSize: 22),
                      ),
                    ),
                ],
              ),
            ),
            if (own) ...[
              ListTile(
                leading: const Icon(Icons.edit),
                title: const Text('编辑'),
                onTap: () async {
                  Navigator.pop(sheetContext);
                  final text =
                      await _prompt(title: '编辑消息', initial: message.content);
                  if (text == null || text.isEmpty || text == message.content) {
                    return;
                  }
                  await widget.services.api
                      .editMessage(message.messageId, text);
                },
              ),
              ListTile(
                leading: const Icon(Icons.delete_outline),
                title: const Text('删除'),
                onTap: () async {
                  Navigator.pop(sheetContext);
                  await widget.services.api.deleteMessage(message.messageId);
                },
              ),
            ],
          ],
        ),
      ),
    );
  }

  Future<String?> _prompt({required String title, String initial = ''}) {
    final controller = TextEditingController(text: initial);
    return showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text(title),
        content: TextField(
          controller: controller,
          autofocus: true,
          onSubmitted: (value) => Navigator.pop(dialogContext, value.trim()),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () =>
                Navigator.pop(dialogContext, controller.text.trim()),
            child: const Text('确定'),
          ),
        ],
      ),
    );
  }

  String _timeOf(int timestamp) {
    final t = DateTime.fromMillisecondsSinceEpoch(timestamp);
    final hh = t.hour.toString().padLeft(2, '0');
    final mm = t.minute.toString().padLeft(2, '0');
    return '$hh:$mm';
  }
}
