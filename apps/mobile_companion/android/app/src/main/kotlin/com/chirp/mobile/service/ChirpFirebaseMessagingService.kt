package com.chirp.mobile.service

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import com.chirp.mobile.MainActivity
import com.chirp.mobile.R
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import org.json.JSONObject

class ChirpFirebaseMessagingService : FirebaseMessagingService() {
    companion object {
        private const val TAG = "ChirpFCM"
        private const val MESSAGE_CHANNEL_ID = "chirp_messages"
        private const val MESSAGE_NOTIFICATION_ID = 2001
    }

    override fun onNewToken(token: String) {
        super.onNewToken(token)
        Log.d(TAG, "New FCM token: $token")

        // Send the token to your Chirp server
        // This would typically be done via the Flutter code
        val prefs = getSharedPreferences("chirp_prefs", Context.MODE_PRIVATE)
        prefs.edit().putString("fcm_token", token).apply()
    }

    override fun onMessageReceived(message: RemoteMessage) {
        super.onMessageReceived(message)

        Log.d(TAG, "Message received from: ${message.from}")

        // Check if message contains a data payload
        if (message.data.isNotEmpty()) {
            val data = message.data
            handleDataMessage(data)
        }

        // Check if message contains notification payload
        message.notification?.let {
            showNotification(it.title, it.body)
        }
    }

    private fun handleDataMessage(data: Map<String, String>) {
        val type = data["type"] ?: return

        when (type) {
            "chat_message" -> {
                val sender = data["sender"] ?: "Unknown"
                val content = data["content"] ?: ""
                val channelId = data["channel_id"] ?: ""
                showChatNotification(sender, content, channelId)
            }
            "incoming_call" -> {
                val callerName = data["caller_name"] ?: "Unknown"
                val roomId = data["room_id"] ?: ""
                VoiceCallNotification.showIncomingCall(this, callerName, roomId)
            }
            "friend_request" -> {
                val fromUser = data["from_user"] ?: "Someone"
                showFriendRequestNotification(fromUser)
            }
            "presence_update" -> {
                // Handle silently - update local presence state
                val userId = data["user_id"] ?: return
                val status = data["status"] ?: return
                updatePresenceStatus(userId, status)
            }
        }
    }

    private fun showChatNotification(sender: String, content: String, channelId: String) {
        createMessageChannel()

        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
            putExtra("action", "open_chat")
            putExtra("channelId", channelId)
        }

        val pendingIntent = PendingIntent.getActivity(
            this,
            channelId.hashCode(),
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, MESSAGE_CHANNEL_ID)
            .setContentTitle(sender)
            .setContentText(content)
            .setSmallIcon(R.drawable.ic_message)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_MESSAGE)
            .setAutoCancel(true)
            .setContentIntent(pendingIntent)
            .build()

        NotificationManagerCompat.from(this).notify(
            channelId.hashCode(),
            notification
        )
    }

    private fun showFriendRequestNotification(fromUser: String) {
        createMessageChannel()

        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
            putExtra("action", "friend_request")
            putExtra("fromUser", fromUser)
        }

        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, MESSAGE_CHANNEL_ID)
            .setContentTitle("Friend Request")
            .setContentText("$fromUser wants to be your friend")
            .setSmallIcon(R.drawable.ic_person_add)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_SOCIAL)
            .setAutoCancel(true)
            .setContentIntent(pendingIntent)
            .build()

        NotificationManagerCompat.from(this).notify(
            fromUser.hashCode(),
            notification
        )
    }

    private fun showNotification(title: String?, body: String?) {
        createMessageChannel()

        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }

        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, MESSAGE_CHANNEL_ID)
            .setContentTitle(title ?: "Chirp")
            .setContentText(body)
            .setSmallIcon(R.drawable.ic_notification)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .setContentIntent(pendingIntent)
            .build()

        NotificationManagerCompat.from(this).notify(0, notification)
    }

    private fun updatePresenceStatus(userId: String, status: String) {
        // Store in SharedPreferences for Flutter to read
        val prefs = getSharedPreferences("chirp_presence", Context.MODE_PRIVATE)
        prefs.edit().putString("presence_$userId", status).apply()
    }

    private fun createMessageChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                MESSAGE_CHANNEL_ID,
                "Messages",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Notifications for new messages"
                setShowBadge(true)
                enableVibration(true)
            }

            val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }
}
