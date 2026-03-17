import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';
import 'package:chirp_mobile/ui/widgets/chat_bubble.dart';

/// Chat screen for 1v1 and group conversations
class ChatScreen extends StatefulWidget {
  final String channelId;
  final String channelName;
  final ChannelType channelType;

  const ChatScreen({
    super.key,
    required this.channelId,
    required this.channelName,
    required this.channelType,
  });

  @override
  State<ChatScreen> createState() => _ChatScreenState();
}

class _ChatScreenState extends State<ChatScreen> {
  final TextEditingController _messageController = TextEditingController();
  final ScrollController _scrollController = ScrollController();
  final List<ChirpMessage> _messages = [];

  bool _isLoading = false;

  @override
  void initState() {
    super.initState();
    _loadHistory();
    _setupMessageListener();
  }

  @override
  void dispose() {
    _messageController.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  void _setupMessageListener() {
    ChirpClient.instance.messageStream.listen((message) {
      if (message.channelId == widget.channelId) {
        setState(() {
          _messages.add(message);
        });
        _scrollToBottom();
      }
    });
  }

  Future<void> _loadHistory() async {
    setState(() => _isLoading = true);

    try {
      final messages = await ChirpClient.instance.getHistory(
        widget.channelId,
        widget.channelType,
        limit: 50,
      );

      setState(() {
        _messages = messages;
        _isLoading = false;
      });

      _scrollToBottom();
    } catch (e) {
      setState(() => _isLoading = false);
    }
  }

  void _scrollToBottom() {
    if (_scrollController.hasClients) {
      Future.delayed(const Duration(milliseconds: 100), () {
        _scrollController.animateTo(
          _scrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 300),
          curve: Curves.easeOut,
        );
      });
    }
  }

  Future<void> _sendMessage() async {
    final text = _messageController.text.trim();
    if (text.isEmpty) return;

    _messageController.clear();

    final success = await ChirpClient.instance.sendMessage(
      widget.channelId,
      text,
    );

    if (!success) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Failed to send message')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.channelName),
        actions: [
          IconButton(
            icon: const Icon(Icons.more_vert),
            onPressed: () {},
          ),
        ],
      ),
      body: Column(
        children: [
          // Messages list
          Expanded(
            child: _isLoading
                ? const Center(child: CircularProgressIndicator())
                : _messages.isEmpty
                    ? Center(
                        child: Text(
                          'No messages yet\nSend a message to start the conversation!',
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.bodyMedium,
                        ),
                      )
                    : ListView.builder(
                        controller: _scrollController,
                        padding: const EdgeInsets.all(8),
                        itemCount: _messages.length,
                        itemBuilder: (context, index) {
                          final message = _messages[index];
                          final isMe = message.senderId == ChirpClient.instance.userId;

                          return ChatBubble(
                            message: message,
                            isMe: isMe,
                          );
                        },
                      ),
          ),

          // Input field
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 8),
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.surface,
              boxShadow: [
                BoxShadow(
                  offset: const Offset(0, -2),
                  blurRadius: 4,
                  color: Colors.black.withOpacity(0.1),
                ),
              ],
            ),
            child: SafeArea(
              child: Row(
                children: [
                  // Attachment button
                  IconButton(
                    icon: const Icon(Icons.attach_file),
                    onPressed: () {},
                  ),

                  // Text field
                  Expanded(
                    child: TextField(
                      controller: _messageController,
                      decoration: const InputDecoration(
                        hintText: 'Type a message...',
                        border: InputBorder.none,
                        contentPadding: EdgeInsets.symmetric(
                          horizontal: 16,
                          vertical: 8,
                        ),
                      ),
                      textInputAction: TextInputAction.newline,
                      onSubmitted: (_) => _sendMessage(),
                    ),
                  ),

                  // Voice message button
                  IconButton(
                    icon: const Icon(Icons.mic),
                    onPressed: () {},
                  ),

                  // Send button
                  IconButton(
                    icon: const Icon(Icons.send),
                    onPressed: _sendMessage,
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
