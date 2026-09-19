import 'dart:async';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:flutter/material.dart';

import '../../api/services.dart';
import '../../api/social_api.dart';
import '../../protocol/errors.dart';
import '../../state/conversation_store.dart';
import '../../state/models.dart';
import '../../state/party_store.dart';
import '../../state/typing_presence.dart';
import 'chat_screen.dart';

/// Post-login shell: four tabs over the shared stores. Every plane degrades
/// independently — the tabs show a quiet "not connected" hint when their
/// websocket is absent/down, while chat keeps working.
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key, required this.services});

  final Services services;

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _tab = 0;

  @override
  void initState() {
    super.initState();
    // Group roster lands in the conversation list; presence pulls ride on
    // friend-list changes.
    unawaited(widget.services.api.refreshGroups());
    widget.services.friends.addListener(_onFriendsChanged);
  }

  @override
  void dispose() {
    widget.services.friends.removeListener(_onFriendsChanged);
    super.dispose();
  }

  void _onFriendsChanged() {
    final social = widget.services.socialApi;
    final friends = widget.services.friends.value.friends;
    if (social != null && friends.isNotEmpty) {
      unawaited(social.pullPresence(friends));
    }
  }

  @override
  Widget build(BuildContext context) {
    final stores = Listenable.merge([
      widget.services.conversations,
      widget.services.friends,
      widget.services.partyState,
      widget.services.devices,
      widget.services.presence,
      widget.services.auth,
    ]);
    return Scaffold(
      appBar: AppBar(
        title: Text(const ['会话', '好友', '组队', '我的'][_tab]),
        actions: _actions(),
      ),
      body: ListenableBuilder(
        listenable: stores,
        builder: (context, _) => switch (_tab) {
          0 => _conversationsTab(),
          1 => _friendsTab(),
          2 => _partyTab(),
          _ => _profileTab(),
        },
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: const [
          NavigationDestination(
              icon: Icon(Icons.chat_bubble_outline),
              selectedIcon: Icon(Icons.chat_bubble),
              label: '会话'),
          NavigationDestination(
              icon: Icon(Icons.people_outline),
              selectedIcon: Icon(Icons.people),
              label: '好友'),
          NavigationDestination(
              icon: Icon(Icons.groups_outlined),
              selectedIcon: Icon(Icons.groups),
              label: '组队'),
          NavigationDestination(
              icon: Icon(Icons.person_outline),
              selectedIcon: Icon(Icons.person),
              label: '我的'),
        ],
      ),
    );
  }

  List<Widget> _actions() {
    switch (_tab) {
      case 0:
        return [
          IconButton(
            tooltip: '发起私聊',
            icon: const Icon(Icons.person_add_alt),
            onPressed: () async {
              final peer = await _prompt(title: '发起私聊', hint: '对方用户 ID');
              if (peer == null || peer.isEmpty) return;
              _openPrivateChat(peer);
            },
          ),
          IconButton(
            tooltip: '创建群组',
            icon: const Icon(Icons.group_add),
            onPressed: () async {
              final name = await _prompt(title: '创建群组', hint: '群组名称');
              if (name == null || name.isEmpty) return;
              final id = await widget.services.api.createGroup(name);
              if (!mounted) return;
              _showSnack(id == null ? '创建失败' : '群组已创建');
            },
          ),
        ];
      case 2:
        final party = widget.services.partyState.value.party;
        if (party == null) return const [];
        final selfId = widget.services.auth.value.userId ?? '';
        final isLeader = isLeaderOf(widget.services.partyState.value, selfId);
        return [
          IconButton(
            tooltip: '邀请入队',
            icon: const Icon(Icons.person_add_alt),
            onPressed: () async {
              final target = await _prompt(title: '邀请入队', hint: '对方用户 ID');
              if (target == null || target.isEmpty) return;
              final code = await widget.services.partyApi!.invite(target);
              if (!mounted) return;
              _showSnack(code == 0 ? '已发送邀请' : _codeText(code));
            },
          ),
          if (isLeader)
            IconButton(
              tooltip: '解散队伍',
              icon: const Icon(Icons.cancel_presentation),
              onPressed: () async {
                final code = await widget.services.partyApi!.disbandParty();
                if (!mounted) return;
                _showSnack(code == 0 ? '队伍已解散' : _codeText(code));
              },
            ),
          IconButton(
            tooltip: '退出队伍',
            icon: const Icon(Icons.exit_to_app),
            onPressed: _leaveParty,
          ),
        ];
      default:
        return const [];
    }
  }

  // ---- 会话 ----

  Widget _conversationsTab() {
    final conversations = widget.services.conversations.value.conversations;
    if (conversations.isEmpty) {
      return const Center(child: Text('暂无会话,点击右上角发起私聊或创建群组'));
    }
    return ListView.builder(
      itemCount: conversations.length,
      itemBuilder: (context, i) {
        final c = conversations[i];
        return ListTile(
          leading: CircleAvatar(
            child: Icon(c.kind == ConversationKind.private
                ? Icons.person
                : Icons.group),
          ),
          title: Text(c.title, maxLines: 1, overflow: TextOverflow.ellipsis),
          subtitle: c.lastMessagePreview == null
              ? null
              : Text(c.lastMessagePreview!,
                  maxLines: 1, overflow: TextOverflow.ellipsis),
          trailing:
              c.unreadLocal > 0 ? Badge(label: Text('${c.unreadLocal}')) : null,
          onTap: () => _openConversation(c),
        );
      },
    );
  }

  Future<void> _openConversation(Conversation c) async {
    await Navigator.push(
      context,
      MaterialPageRoute<void>(
        builder: (_) => ChatScreen(services: widget.services, conversation: c),
      ),
    );
  }

  void _openPrivateChat(String peerId) {
    final selfId = widget.services.auth.value.userId;
    if (selfId == null || peerId == selfId) return;
    final conversation = Conversation(
      kind: ConversationKind.private,
      key: privateKey(selfId, peerId),
      channelId: ([selfId, peerId]..sort()).join('|'),
      peerId: peerId,
      title: peerId,
      unreadLocal: 0,
    );
    upsertConversation(widget.services.conversations, conversation);
    _openConversation(conversation);
  }

  // ---- 好友 ----

  Widget _friendsTab() {
    final social = widget.services.socialApi;
    if (social == null) {
      return const Center(child: Text('社交服务未连接,好友功能暂不可用'));
    }
    final state = widget.services.friends.value;
    final now = DateTime.now().millisecondsSinceEpoch;
    return ListView(
      children: [
        if (state.pendingIn.isNotEmpty) ...[
          const _SectionHeader('收到的好友请求'),
          for (final request in state.pendingIn)
            ListTile(
              leading: const CircleAvatar(child: Icon(Icons.person_add)),
              title: Text(request.fromUserId),
              subtitle: const Text('请求加你为好友'),
              trailing: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  IconButton(
                    tooltip: '接受',
                    icon: Icon(Icons.check,
                        color: Theme.of(context).colorScheme.primary),
                    onPressed: () => unawaited(
                        social.respondRequest(request.requestId, true)),
                  ),
                  IconButton(
                    tooltip: '拒绝',
                    icon: const Icon(Icons.close),
                    onPressed: () => unawaited(
                        social.respondRequest(request.requestId, false)),
                  ),
                ],
              ),
            ),
        ],
        if (state.pendingOut.isNotEmpty) ...[
          const _SectionHeader('已发送的请求'),
          for (final userId in state.pendingOut)
            ListTile(
              enabled: false,
              leading: const CircleAvatar(child: Icon(Icons.schedule)),
              title: Text(userId),
              subtitle: const Text('等待对方处理'),
            ),
        ],
        const _SectionHeader('好友'),
        if (state.friends.isEmpty)
          const Padding(
            padding: EdgeInsets.all(24),
            child: Center(child: Text('还没有好友,点击右上角添加')),
          ),
        for (final userId in state.friends) _friendTile(social, userId, now),
      ],
    );
  }

  Widget _friendTile(SocialApi social, String userId, int now) {
    final subtitle = _presenceText(userId, now);
    return ListTile(
      key: ValueKey('friend-$userId'),
      leading: _presenceAvatar(userId, now),
      title: Text(userId),
      subtitle: subtitle == null ? null : Text(subtitle),
      trailing: PopupMenuButton<String>(
        onSelected: (action) {
          if (action == 'chat') {
            _openPrivateChat(userId);
          } else if (action == 'remove') {
            unawaited(social.removeFriend(userId));
          }
        },
        itemBuilder: (_) => const [
          PopupMenuItem(value: 'chat', child: Text('发消息')),
          PopupMenuItem(value: 'remove', child: Text('删除好友')),
        ],
      ),
      onTap: () => _openPrivateChat(userId),
    );
  }

  Widget _presenceAvatar(String userId, int now) {
    final presence = widget.services.presence.value;
    final online =
        presenceFresh(presence, userId, now) && _isOnline(presence, userId);
    return CircleAvatar(
      child: Icon(online ? Icons.person : Icons.person_off),
    );
  }

  bool _isOnline(PresenceState presence, String userId) {
    final entry = presenceOf(presence, userId);
    return entry != null && entry.status != PresenceStatus.OFFLINE;
  }

  String? _presenceText(String userId, int now) {
    final presence = widget.services.presence.value;
    if (!presenceFresh(presence, userId, now)) return null;
    final entry = presenceOf(presence, userId)!;
    return switch (entry.status) {
      PresenceStatus.ONLINE => '在线',
      PresenceStatus.AWAY => '离开',
      PresenceStatus.DND => '请勿打扰',
      PresenceStatus.IN_GAME => '游戏中',
      _ => '离线',
    };
  }

  // ---- 组队 ----

  Widget _partyTab() {
    final partyApi = widget.services.partyApi;
    if (partyApi == null) {
      return const Center(child: Text('组队服务未连接,组队功能暂不可用'));
    }
    final state = widget.services.partyState.value;
    final selfId = widget.services.auth.value.userId ?? '';
    final isLeader = isLeaderOf(state, selfId);
    return ListView(
      children: [
        for (final invite in state.invites)
          ListTile(
            leading: const CircleAvatar(child: Icon(Icons.mark_email_unread)),
            title: Text('${invite.fromUserId} 邀请你加入队伍'),
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextButton(
                  onPressed: () =>
                      unawaited(partyApi.acceptInvite(invite.inviteId)),
                  child: const Text('接受'),
                ),
                TextButton(
                  onPressed: () =>
                      unawaited(partyApi.declineInvite(invite.inviteId)),
                  child: const Text('拒绝'),
                ),
              ],
            ),
          ),
        if (state.party == null)
          Padding(
            padding: const EdgeInsets.all(48),
            child: Column(
              children: [
                const Text('还没有加入任何队伍'),
                const SizedBox(height: 16),
                FilledButton.icon(
                  icon: const Icon(Icons.groups),
                  label: const Text('创建队伍'),
                  onPressed: () async {
                    final code = await partyApi.createParty();
                    if (!mounted) return;
                    _showSnack(code == 0 ? '队伍已创建' : _codeText(code));
                  },
                ),
              ],
            ),
          )
        else ...[
          const _SectionHeader('队伍成员'),
          ListTile(
            leading: const CircleAvatar(child: Icon(Icons.flag)),
            title: Text('$selfId (我)'),
            trailing: Switch(
              value: selfMemberOf(state, selfId)?.ready ?? false,
              onChanged: (ready) => unawaited(partyApi.setReady(ready)),
            ),
          ),
          for (final member in state.party!.members)
            if (member.userId != selfId)
              ListTile(
                leading: CircleAvatar(
                  child: member.userId == state.party!.leaderId
                      ? const Icon(Icons.flag)
                      : const Icon(Icons.person),
                ),
                title: Text(member.userId),
                subtitle: member.ready ? const Text('已准备') : const Text('未准备'),
                trailing: isLeader
                    ? PopupMenuButton<String>(
                        onSelected: (action) async {
                          if (action == 'transfer') {
                            final code =
                                await partyApi.transferLeader(member.userId);
                            if (!mounted) return;
                            _showSnack(code == 0 ? '已移交队长' : _codeText(code));
                          } else if (action == 'kick') {
                            unawaited(partyApi.kickMember(member.userId));
                          }
                        },
                        itemBuilder: (_) => const [
                          PopupMenuItem(value: 'transfer', child: Text('移交队长')),
                          PopupMenuItem(value: 'kick', child: Text('移出队伍')),
                        ],
                      )
                    : null,
              ),
        ],
      ],
    );
  }

  Future<void> _leaveParty() async {
    final disbanded = await widget.services.partyApi!.leaveParty();
    if (!mounted) return;
    _showSnack(disbanded ? '你离开后队伍已解散' : '已退出队伍');
  }

  // ---- 我的 ----

  Widget _profileTab() {
    final auth = widget.services.auth.value;
    final devices = widget.services.devices.value;
    final deviceApi = widget.services.deviceApi;
    return ListView(
      children: [
        ListTile(
          leading: const CircleAvatar(child: Icon(Icons.person)),
          title: Text(auth.userId ?? '-'),
          subtitle: Text('设备 ID:${auth.deviceId}'),
        ),
        const _SectionHeader('我的设备'),
        if (devices.unavailable)
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Text('设备服务未连接,推送目标暂不可管理'),
          )
        else ...[
          if (!devices.selfRegistered)
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: Text('本机尚未注册为推送目标'),
            ),
          for (final device in devices.devices)
            ListTile(
              leading: const Icon(Icons.devices),
              title: Text(device.deviceName.isEmpty
                  ? device.deviceId
                  : device.deviceName),
              subtitle: Text(
                  '${device.platform} · ${device.osVersion} · ${device.isActive ? '活跃' : '离线'}'),
              trailing: deviceApi != null && deviceApi.isSelf(device.deviceId)
                  ? const Text('本机')
                  : IconButton(
                      tooltip: '移除此设备',
                      icon: const Icon(Icons.delete_outline),
                      onPressed: deviceApi == null
                          ? null
                          : () =>
                              unawaited(deviceApi.unregister(device.deviceId)),
                    ),
            ),
        ],
        const _SectionHeader('通知'),
        ListTile(
          leading: const Icon(Icons.notifications),
          title: const Text('开启系统通知权限'),
          subtitle: const Text('Android 13+ 需要手动授权'),
          onTap: _requestNotificationPermission,
        ),
        const SizedBox(height: 16),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: OutlinedButton.icon(
            icon: const Icon(Icons.logout),
            label: const Text('退出登录'),
            onPressed: _logout,
          ),
        ),
      ],
    );
  }

  Future<void> _requestNotificationPermission() async {
    final notifications = widget.services.notifications;
    if (notifications == null) return;
    final granted = await notifications.requestPermission();
    if (!mounted) return;
    _showSnack(granted ? '已授权' : '未获得授权');
  }

  Future<void> _logout() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('退出登录'),
        content: const Text('退出后需要重新登录才能收发消息。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(dialogContext, true),
            child: const Text('退出'),
          ),
        ],
      ),
    );
    if (confirmed != true) return;
    await widget.services.logoutAll();
  }

  // ---- shared helpers ----

  Future<String?> _prompt({required String title, required String hint}) {
    final controller = TextEditingController();
    return showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text(title),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: InputDecoration(hintText: hint, labelText: hint),
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

  void _showSnack(String message) {
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(message)));
  }

  String _codeText(int code) {
    final parsed = ErrorCode.valueOf(code);
    return parsed == null ? '操作失败($code)' : errorText(parsed);
  }
}

class _SectionHeader extends StatelessWidget {
  const _SectionHeader(this.title);

  final String title;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
        child: Text(
          title,
          style: Theme.of(context)
              .textTheme
              .labelLarge
              ?.copyWith(color: Theme.of(context).colorScheme.primary),
        ),
      );
}
