package com.chirp.mobile.service

import android.app.*
import android.content.Context
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import com.chirp.mobile.R

class VoiceForegroundService : Service() {
    private val binder = LocalBinder()
    private var isRunning = false
    private var currentRoomId: String? = null
    private var currentRoomName: String? = null

    inner class LocalBinder : Binder() {
        fun getService(): VoiceForegroundService = this@VoiceForegroundService
    }

    override fun onBind(intent: Intent?): IBinder {
        return binder
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val roomId = intent.getStringExtra(EXTRA_ROOM_ID) ?: return START_NOT_STICKY
                val roomName = intent.getStringExtra(EXTRA_ROOM_NAME) ?: "Voice Call"
                startForeground(roomId, roomName)
            }
            ACTION_STOP -> {
                stopForeground()
            }
        }
        return START_STICKY
    }

    private fun startForeground(roomId: String, roomName: String) {
        if (isRunning) return

        currentRoomId = roomId
        currentRoomName = roomName
        isRunning = true

        // Create notification channel
        createNotificationChannel()

        // Create notification
        val notification = createNotification(roomName)

        startForeground(NOTIFICATION_ID, notification)
    }

    private fun stopForeground() {
        if (!isRunning) return

        isRunning = false
        currentRoomId = null
        currentRoomName = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Voice Calls",
            NotificationManager.IMPORTANCE_HIGH
        ).apply {
            description = "Notifications for ongoing voice calls"
            setShowBadge(false)
            setSound(null, null)
        }

        val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        notificationManager.createNotificationChannel(channel)
    }

    private fun createNotification(roomName: String): Notification {
        // Create pending intent for opening the app
        val packageName = packageName
        val launchIntent = packageManager.getLaunchIntentForPackage(packageName)
        val pendingIntentFlags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            launchIntent,
            pendingIntentFlags
        )

        // Create actions
        val muteIntent = Intent(this, VoiceBroadcastReceiver::class.java).apply {
            action = ACTION_MUTE
        }
        val mutePendingIntent = PendingIntent.getBroadcast(
            this,
            0,
            muteIntent,
            pendingIntentFlags
        )

        val speakerIntent = Intent(this, VoiceBroadcastReceiver::class.java).apply {
            action = ACTION_SPEAKER
        }
        val speakerPendingIntent = PendingIntent.getBroadcast(
            this,
            0,
            speakerIntent,
            pendingIntentFlags
        )

        val endCallIntent = Intent(this, VoiceBroadcastReceiver::class.java).apply {
            action = ACTION_END_CALL
            putExtra(EXTRA_ROOM_ID, currentRoomId)
        }
        val endCallPendingIntent = PendingIntent.getBroadcast(
            this,
            0,
            endCallIntent,
            pendingIntentFlags
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("In Call: $roomName")
            .setContentText("Tap to return to call")
            .setSmallIcon(R.drawable.ic_call)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_CALL)
            .addAction(
                R.drawable.ic_mic,
                "Mute",
                mutePendingIntent
            )
            .addAction(
                R.drawable.ic_speaker,
                "Speaker",
                speakerPendingIntent
            )
            .addAction(
                R.drawable.ic_call_end,
                "End",
                endCallPendingIntent
            )
            .build()
    }

    fun getCurrentRoomId(): String? = currentRoomId

    fun getCurrentRoomName(): String? = currentRoomName

    companion object {
        const val NOTIFICATION_ID = 1001
        const val CHANNEL_ID = "chirp_voice_channel"

        const val ACTION_START = "com.chirp.mobile.ACTION_START_VOICE_SERVICE"
        const val ACTION_STOP = "com.chirp.mobile.ACTION_STOP_VOICE_SERVICE"
        const val ACTION_MUTE = "com.chirp.mobile.ACTION_MUTE"
        const val ACTION_SPEAKER = "com.chirp.mobile.ACTION_SPEAKER"
        const val ACTION_END_CALL = "com.chirp.mobile.ACTION_END_CALL"

        const val EXTRA_ROOM_ID = "room_id"
        const val EXTRA_ROOM_NAME = "room_name"
    }
}
