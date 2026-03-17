import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';
import 'package:chirp_mobile/ui/screens/chat_screen.dart';

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
class _VoiceScreen extends StatelessWidget {
  const _VoiceScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(Icons.call, size: 64),
          const SizedBox(height: 16),
          const Text('Join a voice room to start talking'),
          const SizedBox(height: 32),
          ElevatedButton.icon(
            onPressed: () {},
            icon: const Icon(Icons.add_call),
            label: const Text('Join Room'),
          ),
        ],
      ),
    );
  }
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
