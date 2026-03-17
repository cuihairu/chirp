import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';
import 'package:chirp_mobile/ui/screens/home_screen.dart';
import 'package:chirp_mobile/ui/screens/login_screen.dart';

void main() {
  runApp(const ChirpApp());
}

class ChirpApp extends StatelessWidget {
  const ChirpApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Chirp - Real-time Communication',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.deepPurple,
          brightness: Brightness.light,
        ),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.deepPurple,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const _AppInitializer(),
    );
  }
}

/// Initializes the app and handles login flow
class _AppInitializer extends StatefulWidget {
  const _AppInitializer({super.key});

  @override
  State<_AppInitializer> createState() => _AppInitializerState();
}

class _AppInitializerState extends State<_AppInitializer> {
  bool _isInitialized = false;
  bool _isLoggedIn = false;

  @override
  void initState() {
    super.initState();
    _initialize();
  }

  Future<void> _initialize() async {
    // Initialize the Chirp SDK
    final success = await ChirpClient.instance.initialize();
    setState(() {
      _isInitialized = true;
      _isLoggedIn = success && ChirpClient.instance.isConnected;
    });
  }

  @override
  Widget build(BuildContext context) {
    if (!_isInitialized) {
      return const Scaffold(
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              CircularProgressIndicator(),
              SizedBox(height: 16),
              Text('Connecting to Chirp...'),
            ],
          ),
        ),
      );
    }

    if (!_isLoggedIn) {
      return const LoginScreen();
    }

    return const HomeScreen();
  }
}
