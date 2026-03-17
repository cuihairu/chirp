import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';

/// Login screen for authentication
class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key});

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final _formKey = GlobalKey<FormState>();
  final _usernameController = TextEditingController();
  final _passwordController = TextEditingController();
  final _deviceIdController = TextEditingController(text: _generateDeviceId());

  bool _isLoading = false;
  String? _errorMessage;

  String _generateDeviceId() {
    // In production, use device_info_plus package
    return 'flutter_${DateTime.now().millisecondsSinceEpoch}';
  }

  Future<void> _login() async {
    if (!_formKey.currentState!.validate()) return;

    setState(() => _isLoading = true);
    _errorMessage = null;

    try {
      // For demo, use username as user_id and password as token
      final userId = _usernameController.text.trim();
      final token = _passwordController.text; // In production, this would be a real JWT

      if (userId.isEmpty || token.isEmpty) {
        setState(() {
          _errorMessage = 'Please enter username and password';
          _isLoading = false;
        });
        return;
      }

      // Connect first
      final connected = await ChirpClient.instance.connect();
      if (!connected) {
        setState(() {
          _errorMessage = 'Failed to connect to server';
          _isLoading = false;
        });
        return;
      }

      // Then login
      final success = await ChirpClient.instance.login(userId, token, _deviceIdController.text);

      setState(() {
        _isLoading = false;
        if (!success) {
          _errorMessage = 'Login failed. Please check your credentials.';
        }
      });

      if (success) {
        // The main app will detect connection state and update
      }
    } catch (e) {
      setState(() {
        _errorMessage = 'An error occurred: $e';
        _isLoading = false;
      });
    }
  }

  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    _deviceIdController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Form(
            key: _formKey,
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Logo or app title
                const Icon(
                  Icons.chat_bubble_rounded,
                  size: 80,
                ),
                const SizedBox(height: 24),
                Text(
                  'Chirp',
                  style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  'Real-time communication for gaming',
                  style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: Colors.grey,
                  ),
                ),
                const SizedBox(height: 48),

                // Error message
                if (_errorMessage != null)
                  Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: Colors.red.shade50,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Row(
                      children: [
                        const Icon(Icons.error, color: Colors.red),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            _errorMessage!,
                            style: const TextStyle(color: Colors.red),
                          ),
                        ),
                        IconButton(
                          icon: const Icon(Icons.close),
                          onPressed: () => setState(() => _errorMessage = null),
                        ),
                      ],
                    ),
                  ),

                const SizedBox(height: 16),

                // Username field
                TextFormField(
                  controller: _usernameController,
                  decoration: const InputDecoration(
                    labelText: 'Username',
                    prefixIcon: Icon(Icons.person),
                    border: OutlineInputBorder(),
                  ),
                  validator: (value) {
                    if (value == null || value.trim().isEmpty) {
                      return 'Please enter username';
                    }
                    return null;
                  },
                  enabled: !_isLoading,
                  textInputAction: TextInputAction.next,
                ),
                const SizedBox(height: 16),

                // Password field
                TextFormField(
                  controller: _passwordController,
                  decoration: const InputDecoration(
                    labelText: 'Password',
                    prefixIcon: Icon(Icons.lock),
                    border: OutlineInputBorder(),
                  ),
                  obscureText: true,
                  validator: (value) {
                    if (value == null || value.isEmpty) {
                      return 'Please enter password';
                    }
                    if (value.length < 6) {
                      return 'Password must be at least 6 characters';
                    }
                    return null;
                  },
                  enabled: !_isLoading,
                  textInputAction: TextInputAction.done,
                  onFieldSubmitted: (_) => _login(),
                ),
                const SizedBox(height: 24),

                // Login button
                SizedBox(
                  height: 48,
                  child: ElevatedButton(
                    onPressed: _isLoading ? null : _login,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Theme.of(context).colorScheme.primary,
                      foregroundColor: Theme.of(context).colorScheme.onPrimary,
                    ),
                    child: _isLoading
                        ? const SizedBox(
                            height: 20,
                            width: 20,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Text('LOGIN'),
                  ),
                ),

                const SizedBox(height: 16),

                // Demo accounts hint
                Text(
                  'Demo: Use any username/password to test',
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Colors.grey,
                  ),
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
