import 'package:flutter/material.dart';

import '../../api/services.dart';
import '../../protocol/errors.dart';

/// Scaffold-phase login: the user id doubles as the token (same as the web
/// companion until real auth lands). A terminal KICK shows its banner here —
/// loginAll re-entering re-registers every plane under the new session.
class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key, required this.services, this.kicked = false});

  final Services services;
  final bool kicked;

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final _userId = TextEditingController();
  bool _busy = false;
  String? _error;

  @override
  void dispose() {
    _userId.dispose();
    super.dispose();
  }

  Future<void> _login() async {
    final userId = _userId.text.trim();
    if (userId.isEmpty || _busy) return;
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      await widget.services.loginAll(userId);
      // The auth store flip routes to HomeScreen via _Root.
    } on RequestError catch (e) {
      if (mounted) setState(() => _error = e.message);
    } catch (_) {
      if (mounted) setState(() => _error = '连接失败,请稍后再试');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Text(
                'Chirp',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 32, fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 8),
              Text(
                '实时聊天 · 好友 · 组队',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodySmall,
              ),
              const SizedBox(height: 32),
              if (widget.kicked) ...[
                const Material(
                  color: Color(0xFFFFF3E0),
                  child: Padding(
                    padding: EdgeInsets.all(12),
                    child: Text(
                      '账号已在其他设备登录,本机会话已下线',
                      style: TextStyle(color: Color(0xFF8D5B00)),
                    ),
                  ),
                ),
                const SizedBox(height: 16),
              ],
              TextField(
                controller: _userId,
                enabled: !_busy,
                decoration: const InputDecoration(
                  labelText: '用户 ID',
                  hintText: '输入用户 ID 登录',
                  border: OutlineInputBorder(),
                ),
                onSubmitted: (_) => _login(),
              ),
              const SizedBox(height: 16),
              FilledButton(
                onPressed: _busy ? null : _login,
                child: _busy
                    ? const SizedBox(
                        width: 20,
                        height: 20,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Text('登录'),
              ),
              if (_error != null) ...[
                const SizedBox(height: 12),
                Text(
                  _error!,
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
