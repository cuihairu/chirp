import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';
import 'package:chirp_mobile/ui/screens/chat_screen.dart';
import 'package:chirp_mobile/ui/screens/voice_room_screen.dart';

/// Main home screen for the Chirp mobile app
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _currentIndex = 0;

  final List<Widget> _screens = [
    const _ChatsListScreen(),
    const _ContactsScreen(),
    const _VoiceScreen(),
    const _ProfileScreen(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: _screens[_currentIndex],
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _currentIndex,
        onTap: (index) => setState(() => _currentIndex = index),
        type: BottomNavigationBarType.fixed,
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.chat_bubble_outline),
            activeIcon: Icon(Icons.chat_bubble),
            label: 'Chats',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.people_outline),
            activeIcon: Icon(Icons.people),
            label: 'Contacts',
          ),
          BottomNavigationBarItem(
            icon: Icon.call,
            label: 'Voice',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.person_outline),
            activeIcon: Icon(Icons.person),
            label: 'Profile',
          ),
        ],
      ),
    );
  }
}

/// Chats list screen
class _ChatsListScreen extends StatelessWidget {
  const _ChatsListScreen({super.key});

  final List<_ChatItem> _recentChats = const [
    _ChatItem(
      id: 'chat_1',
      name: 'Team Alpha',
      lastMessage: 'Ready for raid?',
      timestamp: 1705489000000,
      unreadCount: 3,
    ),
    _ChatItem(
      id: 'chat_2',
      name: 'Alice',
      lastMessage: 'See you tonight!',
      timestamp: 1705488000000,
      unreadCount: 0,
    ),
    _ChatItem(
      id: 'chat_3',
      name: 'Dev Team',
      lastMessage: 'Deploy complete',
      timestamp: 1705487000000,
      unreadCount: 12,
    ),
  ];

  @override
  Widget build(BuildContext context) {
    return ListView.builder(
      itemCount: _recentChats.length,
      itemBuilder: (context, index) {
        final chat = _recentChats[index];
        return _buildChatTile(context, chat);
      },
    );
  }

  Widget _buildChatTile(BuildContext context, _ChatItem chat) {
    return ListTile(
      leading: CircleAvatar(
        child: Text(chat.name[0]),
        backgroundColor: Theme.of(context).colorScheme.primary,
      ),
      title: Text(chat.name),
      subtitle: Text(
        chat.lastMessage,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
      ),
      trailing: chat.unreadCount > 0
          ? Badge(
              label: chat.unreadCount.toString(),
              child: const Icon(Icons.mark_email_unread_rounded),
            )
          : const Text(''),
      onTap: () {
        Navigator.push(
          context,
          MaterialPageRoute(
            builder: (context) => ChatScreen(
              channelId: chat.id,
              channelName: chat.name,
              channelType: ChannelType.private,
            ),
          ),
        );
      },
    );
  }
}

/// Contacts screen
class _ContactsScreen extends StatelessWidget {
  const _ContactsScreen({super.key});

  final List<_ContactItem> _contacts = const [
    _ContactItem(id: 'user_alice', name: 'Alice', status: 'Online'),
    _ContactItem(id: 'user_bob', name: 'Bob', status: 'In Game'),
    _ContactItem(id: 'user_charlie', name: 'Charlie', status: 'Away'),
  ];

  @override
  Widget build(BuildContext context) {
    return ListView.builder(
      itemCount: _contacts.length,
      itemBuilder: (context, index) {
        final contact = _contacts[index];
        return ListTile(
          leading: CircleAvatar(
            child: Text(contact.name[0]),
          ),
          title: Text(contact.name),
          subtitle: Text(contact.status),
          trailing: Icon(
            Icons.message,
            color: Theme.of(context).colorScheme.primary,
          ),
          onTap: () {
            Navigator.push(
              context,
              MaterialPageRoute(
                builder: (context) => ChatScreen(
                  channelId: contact.id,
                  channelName: contact.name,
                  channelType: ChannelType.private,
                ),
              ),
            );
          },
        );
      },
    );
  }
}

/// Voice screen
class _VoiceScreen extends StatefulWidget {
  const _VoiceScreen({super.key});

  @override
  State<_VoiceScreen> createState() => _VoiceScreenState();
}

class _VoiceScreenState extends State<_VoiceScreen> {
  final List<_VoiceRoomItem> _availableRooms = const [
    _VoiceRoomItem(
      id: 'voice_lobby',
      name: 'General Lobby',
      type: VoiceRoomType.channel,
      participants: 5,
    ),
    _VoiceRoomItem(
      id: 'voice_gaming',
      name: 'Gaming Chat',
      type: VoiceRoomType.channel,
      participants: 3,
    ),
    _VoiceRoomItem(
      id: 'voice_strategy',
      name: 'Strategy Room',
      type: VoiceRoomType.group,
      participants: 2,
    ),
  ];

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // Quick join section
        Container(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Voice Channels',
                style: Theme.of(context).textTheme.titleLarge,
              ),
              const SizedBox(height: 8),
              Text(
                'Join a voice channel to start talking with friends',
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                  color: Colors.grey,
                ),
              ),
            ],
          ),
        ),

        const Divider(),

        // Voice rooms list
        Expanded(
          child: ListView.builder(
            padding: const EdgeInsets.all(16),
            itemCount: _availableRooms.length,
            itemBuilder: (context, index) {
              final room = _availableRooms[index];
              return _buildVoiceRoomTile(context, room);
            },
          ),
        ),

        // Create room button
        Container(
          padding: const EdgeInsets.all(16),
          child: SizedBox(
            width: double.infinity,
            child: ElevatedButton.icon(
              onPressed: _showCreateRoomDialog,
              icon: const Icon(Icons.add),
              label: const Text('Create Voice Room'),
              style: ElevatedButton.styleFrom(
                padding: const EdgeInsets.symmetric(vertical: 16),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildVoiceRoomTile(BuildContext context, _VoiceRoomItem room) {
    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      child: ListTile(
        leading: CircleAvatar(
          backgroundColor: room.type == VoiceRoomType.channel
              ? Colors.deepPurple
              : Colors.orange,
          child: Icon(
            room.type == VoiceRoomType.channel
                ? Icons.speaker_notes
                : Icons.group,
            color: Colors.white,
          ),
        ),
        title: Text(room.name),
        subtitle: Text('${room.participants} participants'),
        trailing: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.people,
              size: 16,
              color: Colors.grey.shade600,
            ),
            const SizedBox(width: 4),
            Text(
              '${room.participants}',
              style: TextStyle(color: Colors.grey.shade600),
            ),
            const SizedBox(width: 8),
            const Icon(Icons.arrow_forward_ios, size: 16),
          ],
        ),
        onTap: () => _joinVoiceRoom(context, room),
      ),
    );
  }

  void _joinVoiceRoom(BuildContext context, _VoiceRoomItem room) {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (context) => VoiceRoomScreen(
          roomId: room.id,
          roomName: room.name,
          roomType: room.type,
        ),
      ),
    );
  }

  void _showCreateRoomDialog() {
    final nameController = TextEditingController();
    final formKey = GlobalKey<FormState>();

    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Create Voice Room'),
        content: Form(
          key: formKey,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextFormField(
                controller: nameController,
                decoration: const InputDecoration(
                  labelText: 'Room Name',
                  border: OutlineInputBorder(),
                ),
                validator: (value) {
                  if (value == null || value.trim().isEmpty) {
                    return 'Please enter a room name';
                  }
                  return null;
                },
              ),
              const SizedBox(height: 16),
              DropdownButtonFormField<VoiceRoomType>(
                value: VoiceRoomType.group,
                decoration: const InputDecoration(
                  labelText: 'Room Type',
                  border: OutlineInputBorder(),
                ),
                items: const [
                  DropdownMenuItem(
                    value: VoiceRoomType.peerToPeer,
                    child: Text('1:1 Call'),
                  ),
                  DropdownMenuItem(
                    value: VoiceRoomType.group,
                    child: Text('Group Call'),
                  ),
                  DropdownMenuItem(
                    value: VoiceRoomType.channel,
                    child: Text('Channel'),
                  ),
                ],
                onChanged: (value) {},
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () {
              if (formKey.currentState!.validate()) {
                Navigator.pop(context);
                // Create and join the room
                _createAndJoinRoom(nameController.text.trim());
              }
            },
            child: const Text('Create'),
          ),
        ],
      ),
    );
  }

  void _createAndJoinRoom(String roomName) async {
    final roomId = await ChirpClient.instance.createVoiceRoom(
      VoiceRoomType.group,
      roomName,
      10, // max participants
    );

    if (roomId != null && mounted) {
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => VoiceRoomScreen(
            roomId: roomId,
            roomName: roomName,
            roomType: VoiceRoomType.group,
          ),
        ),
      );
    }
  }
}

// Voice room item
class _VoiceRoomItem {
  final String id;
  final String name;
  final VoiceRoomType type;
  final int participants;

  const _VoiceRoomItem({
    required this.id,
    required this.name,
    required this.type,
    required this.participants,
  });
}

/// Profile screen
class _ProfileScreen extends StatelessWidget {
  const _ProfileScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final userId = ChirpClient.instance.userId;

    return ListView(
      children: [
        const SizedBox(height: 100),
        Center(
          child: CircleAvatar(
            radius: 50,
            child: Text(userId.isNotEmpty ? userId[0].toUpperCase() : '?'),
          ),
        ),
        const SizedBox(height: 16),
        Center(
          child: Text(
            userId.isNotEmpty ? userId : 'Not logged in',
            style: Theme.of(context).textTheme.titleLarge,
          ),
        ),
        const SizedBox(height: 32),
        _buildSettingTile(context, Icons.settings, 'Settings'),
        _buildSettingTile(context, Icons.notifications, 'Notifications'),
        _buildSettingTile(context, Icons.security, 'Privacy & Security'),
        _buildSettingTile(context, Icons.info_outline, 'About'),
        const Divider(),
        _buildSettingTile(context, Icons.logout, 'Logout', isDestructive: true),
      ],
    );
  }

  Widget _buildSettingTile(BuildContext context, IconData icon, String title, {bool isDestructive = false}) {
    return ListTile(
      leading: Icon(icon),
      title: Text(title),
      trailing: const Icon(Icons.chevron_right),
      onTap: () {
        // Handle settings navigation
      },
    );
  }
}

// Supporting classes

class _ChatItem {
  final String id;
  final String name;
  final String lastMessage;
  final int timestamp;
  final int unreadCount;

  const _ChatItem({
    required this.id,
    required this.name,
    required this.lastMessage,
    required this.timestamp,
    this.unreadCount = 0,
  });
}

class _ContactItem {
  final String id;
  final String name;
  final String status;

  const _ContactItem({
    required this.id,
    required this.name,
    required this.status,
  });
}

class Badge extends StatelessWidget {
  final Widget child;
  final String label;

  const Badge({super.key, required this.child, required this.label});

  @override
  Widget build(BuildContext context) {
    return Stack(
      clipBehavior: Clip.none,
      children: [
        child,
        Positioned(
          right: 0,
          top: 0,
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            decoration: BoxDecoration(
              color: Colors.red,
              borderRadius: BorderRadius.circular(10),
            ),
            constraints: const BoxConstraints(
              minWidth: 16,
              minHeight: 16,
            ),
            child: Center(
              child: Text(
                label,
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 10,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}
