import 'package:flutter/material.dart';

import '../api/chat_api.dart';
import '../api/local_notifications.dart';
import '../api/notify_logic.dart';
import '../api/services.dart';
import '../protocol/chirp_client.dart';
import '../state/models.dart';
import 'screens/home_screen.dart';
import 'screens/login_screen.dart';

/// Root widget: routes between login and home from the auth store, handles
/// the terminal kick (back to login with a banner), and raises local
/// notifications from the live chat stream — the mobile twin of the web's
/// desktop-notification surface.
class ChirpApp extends StatefulWidget {
  const ChirpApp(
      {super.key, required this.services, required this.notifications});

  final Services services;
  final LocalNotifications notifications;

  @override
  State<ChirpApp> createState() => _ChirpAppState();
}

class _ChirpAppState extends State<ChirpApp> with WidgetsBindingObserver {
  bool _appInForeground = true;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    widget.services.api.onMessage(_onMessage);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _appInForeground = state == AppLifecycleState.resumed;
    // Background tabs stop timers on the web; on mobile the process keeps
    // running, so an immediate heartbeat on resume mirrors "回前台补心跳".
    if (_appInForeground) {
      widget.services.client.heartbeatNow();
    }
  }

  void _onMessage(IncomingMessage message) {
    if (!shouldNotifyFor(
      appInForeground: _appInForeground,
      openChannelKey: widget.services.api.activeChannelKey,
      messageChannelKey: message.channel.key,
    )) {
      return;
    }
    final isPrivate = message.channel.kind == ConversationKind.private;
    final title = isPrivate
        ? privateTitle(message.fromUserId)
        : groupTitle(message.channel.peerId, message.fromUserId);
    widget.notifications.show(
      title: title,
      body: message.content,
      channelKey: message.channel.key,
      id: message.channel.key.hashCode & 0x7fffffff,
    );
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Chirp',
      theme: ThemeData(colorSchemeSeed: Colors.deepPurple, useMaterial3: true),
      darkTheme: ThemeData(
          brightness: Brightness.dark,
          colorSchemeSeed: Colors.deepPurple,
          useMaterial3: true),
      home: Directionality(
        textDirection: TextDirection.ltr,
        child: _Root(
          services: widget.services,
          foreground: () => _appInForeground,
        ),
      ),
    );
  }
}

class _Root extends StatefulWidget {
  const _Root({required this.services, required this.foreground});

  final Services services;
  final bool Function() foreground;

  @override
  State<_Root> createState() => _RootState();
}

class _RootState extends State<_Root> {
  @override
  void initState() {
    super.initState();
    widget.services.auth.addListener(_onAuth);
    widget.services.client.onStatus(_onStatus);
  }

  @override
  void dispose() {
    widget.services.auth.removeListener(_onAuth);
    super.dispose();
  }

  void _onAuth() {
    if (mounted) setState(() {});
  }

  /// The terminal chat kick (session taken over by another device) routes
  /// back to login with the banner; plain drops just auto-reconnect.
  void _onStatus(ConnStatus status) {
    if (status != ConnStatus.kicked) return;
    widget.services.auth
        .update((prev) => prev.copyWith(kicked: true, loggedIn: false));
  }

  @override
  Widget build(BuildContext context) {
    final auth = widget.services.auth.value;
    if (auth.loggedIn && !auth.kicked) {
      return HomeScreen(services: widget.services);
    }
    return LoginScreen(
      services: widget.services,
      kicked: auth.kicked,
    );
  }
}
