package com.chirp.mobile

import android.os.Bundle
import android.content.Intent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity: FlutterActivity() {
    private val CHANNEL = "com.chirp.mobile/native"
    private var methodChannel: MethodChannel? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        methodChannel = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
        methodChannel?.setMethodCallHandler { call, result ->
            when (call.method) {
                "getPlatformVersion" -> {
                    result.success("Android ${android.os.Build.VERSION.RELEASE}")
                }
                "getDeviceId" -> {
                    result.success(getDeviceId())
                }
                "startForegroundService" -> {
                    val roomId = call.argument<String>("roomId")
                    val roomName = call.argument<String>("roomName")
                    if (roomId != null && roomName != null) {
                        startVoiceForegroundService(roomId, roomName)
                        result.success(true)
                    } else {
                        result.error("INVALID_ARGS", "Missing roomId or roomName", null)
                    }
                }
                "stopForegroundService" -> {
                    stopVoiceForegroundService()
                    result.success(true)
                }
                "showIncomingCallNotification" -> {
                    val callerName = call.argument<String>("callerName")
                    val roomId = call.argument<String>("roomId")
                    if (callerName != null && roomId != null) {
                        VoiceCallNotification.showIncomingCall(this, callerName, roomId)
                        result.success(true)
                    } else {
                        result.error("INVALID_ARGS", "Missing callerName or roomId", null)
                    }
                }
                "requestAudioFocus" -> {
                    result.success(requestAudioFocus())
                }
                "abandonAudioFocus" -> {
                    result.success(abandonAudioFocus())
                }
                "setSpeakerphoneOn" -> {
                    val on = call.argument<Boolean>("on") ?: false
                    result.success(setSpeakerphoneOn(on))
                }
                else -> {
                    result.notImplemented()
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopVoiceForegroundService()
    }

    private fun getDeviceId(): String {
        return android.provider.Settings.Secure.getString(
            contentResolver,
            android.provider.Settings.Secure.ANDROID_ID
        ) ?: "unknown"
    }

    private fun startVoiceForegroundService(roomId: String, roomName: String) {
        val intent = Intent(this, VoiceForegroundService::class.java).apply {
            putExtra(VoiceForegroundService.EXTRA_ROOM_ID, roomId)
            putExtra(VoiceForegroundService.EXTRA_ROOM_NAME, roomName)
            action = VoiceForegroundService.ACTION_START
        }
        startForegroundService(intent)
    }

    private fun stopVoiceForegroundService() {
        val intent = Intent(this, VoiceForegroundService::class.java).apply {
            action = VoiceForegroundService.ACTION_STOP
        }
        startService(intent)
    }

    private fun requestAudioFocus(): Boolean {
        val audioManager = getSystemService(android.content.Context.AUDIO_SERVICE) as android.media.AudioManager
        val result = audioManager.requestAudioFocus(
            { },
            android.media.AudioManager.STREAM_VOICE_CALL,
            android.media.AudioManager.AUDIOFOCUS_GAIN_TRANSIENT
        )
        return result == android.media.AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    }

    private fun abandonAudioFocus(): Boolean {
        val audioManager = getSystemService(android.content.Context.AUDIO_SERVICE) as android.media.AudioManager
        val result = audioManager.abandonAudioFocus { }
        return result == android.media.AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    }

    private fun setSpeakerphoneOn(on: Boolean): Boolean {
        val audioManager = getSystemService(android.content.Context.AUDIO_SERVICE) as android.media.AudioManager
        audioManager.isSpeakerphoneOn = on
        return audioManager.isSpeakerphoneOn == on
    }

    companion object {
        const val TAG = "MainActivity"
    }
}
