package com.chirp.mobile.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import io.flutter.plugin.common.MethodChannel
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.dart.DartExecutor
import io.flutter.embedding.engine.loader.FlutterEngineGroup

class VoiceBroadcastReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
        if (context == null || intent == null) return

        when (intent.action) {
            VoiceForegroundService.ACTION_MUTE -> {
                toggleMicrophone(context)
            }
            VoiceForegroundService.ACTION_SPEAKER -> {
                toggleSpeaker(context)
            }
            VoiceForegroundService.ACTION_END_CALL -> {
                val roomId = intent.getStringExtra(VoiceForegroundService.EXTRA_ROOM_ID)
                endCall(context, roomId)
            }
        }
    }

    private fun toggleMicrophone(context: Context) {
        // Send event to Flutter to handle actual mute state
        // The Flutter side will handle the WebRTC track enable/disable
        sendEventToFlutter(context, "microphone_toggled", null)
    }

    private fun toggleSpeaker(context: Context) {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val isOn = audioManager.isSpeakerphoneOn
        audioManager.isSpeakerphoneOn = !isOn
        sendEventToFlutter(context, "speaker_toggled", !isOn)
    }

    private fun endCall(context: Context, roomId: String?) {
        // Stop the foreground service
        val serviceIntent = Intent(context, VoiceForegroundService::class.java).apply {
            action = VoiceForegroundService.ACTION_STOP
        }
        context.startService(serviceIntent)

        // Send event to Flutter
        val args = mapOf("roomId" to roomId)
        sendEventToFlutter(context, "call_ended", args)
    }

    private fun sendEventToFlutter(context: Context, event: String, args: Map<String, Any?>?) {
        // This would typically use a singleton FlutterEngine reference
        // For simplicity, we'll use a SharedPreferences flag that Flutter polls
        val prefs = context.getSharedPreferences("chirp_events", Context.MODE_PRIVATE)
        prefs.edit().apply {
            putString("last_event", event)
            putString("last_event_args", args?.toString() ?: "")
            apply()
        }
    }
}
