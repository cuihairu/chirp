import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'package:chirp_mobile/core/sdk/chirp_client.dart';

/// Voice room screen for real-time voice communication
class VoiceRoomScreen extends StatefulWidget {
  final String roomId;
  final String roomName;
  final VoiceRoomType roomType;

  const VoiceRoomScreen({
    super.key,
    required this.roomId,
    required this.roomName,
    required this.roomType,
  });

  @override
  State<VoiceRoomScreen> createState() => _VoiceRoomScreenState();
}

class _VoiceRoomScreenState extends State<VoiceRoomScreen> {
  bool _isMuted = false;
  bool _isSpeakerOn = false;
  bool _isConnecting = true;
  bool _isConnected = false;
  final List<VoiceParticipant> _participants = [];

  RTCPeerConnection? _peerConnection;
  MediaStream? _localStream;
  final Map<String, RTCPeerConnection> _peerConnections = {};
  final Map<String, MediaStream> _remoteStreams = {};

  @override
  void initState() {
    super.initState();
    _initializeVoiceRoom();
  }

  @override
  void dispose() {
    _cleanup();
    super.dispose();
  }

  Future<void> _initializeVoiceRoom() async {
    try {
      // Get initial room info
      final roomInfo = await ChirpClient.instance.getVoiceRoomInfo(widget.roomId);
      if (roomInfo != null) {
        setState(() {
          _participants.clear();
          _participants.addAll(roomInfo.participants);
        });
      }

      // Setup WebRTC
      await _setupWebRTC();

      // Join the voice room
      final success = await ChirpClient.instance.joinVoiceRoom(
        widget.roomId,
        widget.roomType,
      );

      if (success) {
        setState(() {
          _isConnected = true;
          _isConnecting = false;
        });

        // Listen for participant events
        _setupEventListeners();
      } else {
        setState(() => _isConnecting = false);
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('Failed to join voice room')),
          );
        }
      }
    } catch (e) {
      setState(() => _isConnecting = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error: $e')),
        );
      }
    }
  }

  Future<void> _setupWebRTC() async {
    // Create local audio stream
    final stream = await navigator.mediaDevices.getUserMedia(
      const MediaStreamConstraints(audio: true),
    );
    setState(() {
      _localStream = stream;
    });

    // Set up peer connection configuration
    final configuration = <String, dynamic>{
      'iceServers': [
        {'urls': 'stun:stun.l.google.com:19302'},
        {'urls': 'stun:stun1.l.google.com:19302'},
      ],
      'sdpSemantics': 'unified-plan',
    };

    _peerConnection = await createPeerConnection(configuration);

    // Handle ICE candidates
    _peerConnection!.onIceCandidate = (candidate) {
      // Send ICE candidate through Chirp signaling
      ChirpClient.instance.sendIceCandidate(
        widget.roomId,
        candidate.candidate!,
        candidate.sdpMid!,
        candidate.sdpMLineIndex!,
      );
    };

    // Handle remote stream
    _peerConnection!.onAddStream = (stream) {
      setState(() {
        // Track remote stream
      });
    };
  }

  void _setupEventListeners() {
    // Listen for participant joined events
    ChirpClient.instance.voiceParticipantJoinedStream.listen((participant) {
      setState(() {
        _participants.add(participant);
        _setupPeerConnection(participant.userId);
      });
    });

    // Listen for participant left events
    ChirpClient.instance.voiceParticipantLeftStream.listen((userId) {
      setState(() {
        _participants.removeWhere((p) => p.userId == userId);
        _cleanupPeerConnection(userId);
      });
    });

    // Listen for speaking state changes
    ChirpClient.instance.voiceSpeakingStream.listen((event) {
      setState(() {
        final idx = _participants.indexWhere((p) => p.userId == event.userId);
        if (idx != -1) {
          _participants[idx] = VoiceParticipant(
            userId: _participants[idx].userId,
            username: _participants[idx].username,
            isSpeaking: event.isSpeaking,
            isMuted: _participants[idx].isMuted,
          );
        }
      });
    });

    // Listen for ICE candidates from server
    ChirpClient.instance.voiceIceCandidateStream.listen((candidate) {
      _handleIceCandidate(candidate);
    });

    // Listen for SDP offers from server
    ChirpClient.instance.voiceSdpOfferStream.listen((offer) {
      _handleSdpOffer(offer);
    });
  }

  Future<void> _setupPeerConnection(String userId) async {
    final configuration = <String, dynamic>{
      'iceServers': [
        {'urls': 'stun:stun.l.google.com:19302'},
      ],
    };

    final pc = await createPeerConnection(configuration);

    // Add local stream
    if (_localStream != null) {
      await pc.addStream(_localStream!);
    }

    // Handle ICE candidates
    pc.onIceCandidate = (candidate) {
      ChirpClient.instance.sendIceCandidate(
        widget.roomId,
        candidate.candidate!,
        candidate.sdpMid!,
        candidate.sdpMLineIndex!,
        toUserId: userId,
      );
    };

    // Handle remote stream
    pc.onAddStream = (stream) {
      setState(() {
        _remoteStreams[userId] = stream;
      });
    };

    setState(() {
      _peerConnections[userId] = pc;
    });
  }

  void _cleanupPeerConnection(String userId) {
    _peerConnections[userId]?.close();
    _peerConnections.remove(userId);
    _remoteStreams.remove(userId);
  }

  Future<void> _handleIceCandidate(VoiceIceCandidateEvent candidate) async {
    final pc = _peerConnections[candidate.fromUserId];
    if (pc != null) {
      await pc.addCandidate(candidate.candidate, candidate.sdpMid, candidate.sdpMLineIndex);
    }
  }

  Future<void> _handleSdpOffer(VoiceSdpOfferEvent offer) async {
    final pc = _peerConnections[offer.fromUserId];
    if (pc == null) return;

    // Set remote description
    await pc.setRemoteDescription(offer.sdpOffer);

    // Create answer
    final answer = await pc.createAnswer();
    await pc.setLocalDescription(answer);

    // Send answer back
    ChirpClient.instance.sendSdpAnswer(
      widget.roomId,
      answer.sdp!,
      offer.fromUserId,
    );
  }

  void _cleanup() {
    // Leave room
    ChirpClient.instance.leaveVoiceRoom(widget.roomId);

    // Close all peer connections
    for (final pc in _peerConnections.values) {
      pc.close();
    }
    _peerConnections.clear();

    // Dispose local stream
    _localStream?.dispose();
    _localStream = null;

    // Dispose remote streams
    for (final stream in _remoteStreams.values) {
      stream.dispose();
    }
    _remoteStreams.clear();
  }

  void _toggleMute() {
    if (_localStream != null) {
      final audioTrack = _localStream!.getAudioTracks()[0];
      audioTrack.enabled = !audioTrack.enabled;
      setState(() {
        _isMuted = !audioTrack.enabled;
      });

      // Notify server
      ChirpClient.instance.setVoiceMute(widget.roomId, _isMuted);
    }
  }

  void _toggleSpeaker() {
    setState(() {
      _isSpeakerOn = !_isSpeakerOn;
    });
    // Use speakerphone for output
    if (_localStream != null) {
      // Implementation depends on platform
    }
  }

  void _leaveRoom() {
    _cleanup();
    Navigator.pop(context);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey.shade900,
      appBar: AppBar(
        title: Text(widget.roomName),
        backgroundColor: Colors.transparent,
        elevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back, color: Colors.white),
          onPressed: _leaveRoom,
        ),
        actions: [
          if (_isConnecting)
            const Padding(
              padding: EdgeInsets.all(16),
              child: SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white),
              ),
            ),
        ],
      ),
      body: Column(
        children: [
          // Room info
          Container(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                Icon(
                  widget.roomType == VoiceRoomType.channel
                      ? Icons.speaker_notes
                      : Icons.group,
                  size: 64,
                  color: Colors.white,
                ),
                const SizedBox(height: 8),
                Text(
                  widget.roomName,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 24,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  '${_participants.length} participant${_participants.length != 1 ? "s" : ""}',
                  style: TextStyle(
                    color: Colors.grey.shade400,
                    fontSize: 14,
                  ),
                ),
              ],
            ),
          ),

          const SizedBox(height: 16),

          // Participants list
          Expanded(
            child: _participants.isEmpty
                ? Center(
                    child: Text(
                      _isConnecting ? 'Connecting...' : 'No participants yet',
                      style: TextStyle(color: Colors.grey.shade400),
                    ),
                  )
                : ListView.builder(
                    padding: const EdgeInsets.symmetric(horizontal: 16),
                    itemCount: _participants.length,
                    itemBuilder: (context, index) {
                      final participant = _participants[index];
                      return _buildParticipantTile(participant);
                    },
                  ),
          ),

          // Controls
          Container(
            padding: const EdgeInsets.all(24),
            decoration: BoxDecoration(
              color: Colors.grey.shade800,
              borderRadius: const BorderRadius.vertical(top: Radius.circular(24)),
            ),
            child: SafeArea(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  _buildControlButton(
                    icon: _isMuted ? Icons.mic_off : Icons.mic,
                    label: 'Mute',
                    isActive: _isMuted,
                    onPressed: _toggleMute,
                  ),
                  _buildControlButton(
                    icon: _isSpeakerOn ? Icons.volume_up : Icons.volume_down,
                    label: 'Speaker',
                    isActive: _isSpeakerOn,
                    onPressed: _toggleSpeaker,
                  ),
                  _buildControlButton(
                    icon: Icons.call_end,
                    label: 'Leave',
                    isDestructive: true,
                    onPressed: _leaveRoom,
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildParticipantTile(VoiceParticipant participant) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.grey.shade800,
        borderRadius: BorderRadius.circular(12),
        border: participant.isSpeaking
            ? Border.all(color: Colors.green, width: 2)
            : null,
      ),
      child: Row(
        children: [
          CircleAvatar(
            backgroundColor: participant.isSpeaking
                ? Colors.green
                : Theme.of(context).colorScheme.primary,
            child: Text(
              participant.username.isNotEmpty ? participant.username[0].toUpperCase() : '?',
              style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
            ),
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  participant.username,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 16,
                    fontWeight: FontWeight.w500,
                  ),
                ),
                if (participant.isMuted)
                  Text(
                    'Muted',
                    style: TextStyle(
                      color: Colors.grey.shade400,
                      fontSize: 12,
                    ),
                  ),
                if (participant.isSpeaking)
                  Text(
                    'Speaking...',
                    style: TextStyle(
                      color: Colors.green.shade400,
                      fontSize: 12,
                    ),
                  ),
              ],
            ),
          ),
          if (participant.isSpeaking)
            Icon(
              Icons.graphic_eq,
              color: Colors.green.shade400,
            ),
        ],
      ),
    );
  }

  Widget _buildControlButton({
    required IconData icon,
    required String label,
    bool isActive = false,
    bool isDestructive = false,
    required VoidCallback onPressed,
  }) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 64,
          height: 64,
          decoration: BoxDecoration(
            color: isDestructive
                ? Colors.red
                : isActive
                    ? Colors.white
                    : Colors.grey.shade700,
            shape: BoxShape.circle,
          ),
          child: IconButton(
            icon: Icon(
              icon,
              size: 28,
              color: isDestructive
                  ? Colors.white
                  : isActive
                      ? Colors.black
                      : Colors.white,
            ),
            onPressed: onPressed,
          ),
        ),
        const SizedBox(height: 8),
        Text(
          label,
          style: TextStyle(
            color: Colors.grey.shade400,
            fontSize: 12,
          ),
        ),
      ],
    );
  }
}

// Supporting classes

enum VoiceRoomType {
  peerToPeer,
  group,
  channel,
}

class VoiceParticipant {
  final String userId;
  final String username;
  final bool isSpeaking;
  final bool isMuted;

  VoiceParticipant({
    required this.userId,
    required this.username,
    this.isSpeaking = false,
    this.isMuted = false,
  });
}

class VoiceRoomInfo {
  final String roomId;
  final String roomName;
  final VoiceRoomType roomType;
  final List<VoiceParticipant> participants;

  VoiceRoomInfo({
    required this.roomId,
    required this.roomName,
    required this.roomType,
    required this.participants,
  });
}

class VoiceIceCandidateEvent {
  final String fromUserId;
  final String candidate;
  final String sdpMid;
  final int sdpMLineIndex;

  VoiceIceCandidateEvent({
    required this.fromUserId,
    required this.candidate,
    required this.sdpMid,
    required this.sdpMLineIndex,
  });
}

class VoiceSdpOfferEvent {
  final String fromUserId;
  final String sdpOffer;

  VoiceSdpOfferEvent({
    required this.fromUserId,
    required this.sdpOffer,
  });
}

class VoiceSpeakingEvent {
  final String userId;
  final bool isSpeaking;

  VoiceSpeakingEvent({
    required this.userId,
    required this.isSpeaking,
  });
}
