package com.chirp.mobile.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.app.NotificationCompat
import com.chirp.mobile.MainActivity
import com.chirp.mobile.R

object VoiceCallNotification {
    private const val INCOMING_CALL_CHANNEL_ID = "chirp_incoming_call"
    private const val INCOMING_CALL_NOTIFICATION_ID = 1002

    fun createNotificationChannel(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                INCOMING_CALL_CHANNEL_ID,
                "Incoming Calls",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Notifications for incoming voice calls"
                setShowBadge(true)
                enableVibration(true)
                setSound(
                    android.provider.Settings.System.DEFAULT_RINGTONE_URI,
                    android.media.AudioAttributes.Builder()
                        .setUsage(android.media.AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
                        .setContentType(android.media.AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
            }

            val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }

    fun showIncomingCall(context: Context, callerName: String, roomId: String) {
        createNotificationChannel(context)

        // Create full screen intent for the incoming call UI
        val fullScreenIntent = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            putExtra("action", "incoming_call")
            putExtra("callerName", callerName)
            putExtra("roomId", roomId)
        }

        val fullScreenPendingIntent = PendingIntent.getActivity(
            context,
            0,
            fullScreenIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Create accept intent
        val acceptIntent = Intent(context, VoiceBroadcastReceiver::class.java).apply {
            action = "com.chirp.mobile.ACCEPT_CALL"
            putExtra("roomId", roomId)
            putExtra("callerName", callerName)
        }
        val acceptPendingIntent = PendingIntent.getBroadcast(
            context,
            0,
            acceptIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Create decline intent
        val declineIntent = Intent(context, VoiceBroadcastReceiver::class.java).apply {
            action = "com.chirp.mobile.DECLINE_CALL"
            putExtra("roomId", roomId)
        }
        val declinePendingIntent = PendingIntent.getBroadcast(
            context,
            0,
            declineIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        // Build notification
        val notification = NotificationCompat.Builder(context, INCOMING_CALL_CHANNEL_ID)
            .setContentTitle("Incoming Call")
            .setContentText(callerName)
            .setSmallIcon(R.drawable.ic_call)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_CALL)
            .setFullScreenIntent(fullScreenPendingIntent, true)
            .setOngoing(true)
            .setAutoCancel(false)
            .addAction(
                R.drawable.ic_call_decline,
                "Decline",
                declinePendingIntent
            )
            .addAction(
                R.drawable.ic_call_answer,
                "Answer",
                acceptPendingIntent
            )
            .build()

        // Show notification
        val notificationManager = NotificationManagerCompat.from(context)
        notificationManager.notify(INCOMING_CALL_NOTIFICATION_ID, notification)
    }

    fun dismissIncomingCall(context: Context) {
        val notificationManager = NotificationManagerCompat.from(context)
        notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    }
}
