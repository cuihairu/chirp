package chirp.mobile

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import chirp.app_notification.AppNotification
import chirp.common.Common
import chirp.mobile.databinding.ActivityNotificationsBinding
import chirp.mobile.databinding.ItemNotifBinding
import chirp.mobile.protocol.MsgSpecs
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Notification center (design 04). Rows come from the session's live feed
 * (new messages outside the open chat, friend requests, party invites, push
 * registration) — the protocol has no server-side notification history, and
 * the note on screen says so. Unread rows (newest first) carry the accent
 * wash until 全部已读. MESSAGE rows deep-link into the chat window, FRIEND
 * and PARTY rows jump to their tabs in the home shell.
 */
class NotificationsActivity : AppCompatActivity() {

    private lateinit var binding: ActivityNotificationsBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityNotificationsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.backBtn.setOnClickListener { finish() }
        binding.markReadBtn.setOnClickListener {
            chirpSession.markFeedRead()
            render()
        }
        render()
        loadPushState()
    }

    private fun render() {
        val feed = chirpSession.notificationFeed()
        val unread = chirpSession.feedUnreadCount()
        binding.notifList.removeAllViews()
        binding.notifListEarlier.removeAllViews()

        val today = LocalDate.now()
        val zone = ZoneId.systemDefault()
        var shown = 0
        val todayRows = ArrayList<View>()
        val earlierRows = ArrayList<View>()
        feed.forEachIndexed { index, item ->
            val row = ItemNotifBinding.inflate(layoutInflater, binding.notifList, false)
            bindRow(row, item, index < unread)
            row.root.setOnClickListener { open(item) }
            val date = Instant.ofEpochMilli(item.timeMs).atZone(zone).toLocalDate()
            (if (date == today) todayRows else earlierRows).add(row.root)
            shown++
        }
        todayRows.forEach { binding.notifList.addView(it) }
        earlierRows.forEach { binding.notifListEarlier.addView(it) }

        binding.sectionToday.visibility = if (todayRows.isEmpty()) View.GONE else View.VISIBLE
        binding.sectionEarlier.visibility = if (earlierRows.isEmpty()) View.GONE else View.VISIBLE
        binding.notifEmpty.visibility = if (shown == 0) View.VISIBLE else View.GONE
    }

    private fun bindRow(row: ItemNotifBinding, item: NotifItem, unread: Boolean) {
        row.notifIcon.setImageResource(item.iconRes())
        row.notifTitle.text = item.title
        row.notifBody.text = item.body
        row.notifKind.setText(item.kindRes())
        row.notifTime.text = DateTimeFormatter.ofPattern(
            if (Instant.ofEpochMilli(item.timeMs).atZone(ZoneId.systemDefault()).toLocalDate() ==
                LocalDate.now()
            ) "HH:mm" else "MM-dd",
        ).format(Instant.ofEpochMilli(item.timeMs).atZone(ZoneId.systemDefault()))
        row.root.setBackgroundColor(
            ContextCompat.getColor(
                this,
                if (unread) R.color.chirp_accent_dim else R.color.chirp_surface,
            ),
        )
    }

    private fun open(item: NotifItem) {
        when (item.kind) {
            NotifItem.Kind.MESSAGE -> {
                val id = item.channelId ?: return
                startActivity(
                    Intent(this, ChatActivity::class.java)
                        .putExtra(ChatActivity.EXTRA_CHANNEL_ID, id)
                        .putExtra(ChatActivity.EXTRA_IS_GROUP, item.isGroup)
                        .putExtra(ChatActivity.EXTRA_TITLE, item.title),
                )
            }
            NotifItem.Kind.FRIEND ->
                startActivity(
                    Intent(this, HomeActivity::class.java)
                        .setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                        .putExtra(HomeActivity.EXTRA_TAB, HomeActivity.TAB_FRIENDS),
                )
            NotifItem.Kind.PARTY ->
                startActivity(
                    Intent(this, HomeActivity::class.java)
                        .setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
                        .putExtra(HomeActivity.EXTRA_TAB, HomeActivity.TAB_PARTY),
                )
            NotifItem.Kind.SYSTEM -> Unit
        }
    }

    /** The 系统推送 row reads real registration state, same as the devices tab. */
    private fun loadPushState() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getUserDevices,
            AppNotification.GetUserDevicesRequest.newBuilder().setUserId(self).build(),
        ).whenComplete { resp, err ->
            runOnUiThread {
                if (err != null || resp?.code != Common.ErrorCode.OK) {
                    binding.pushChip.setText(R.string.devices_service_unavailable)
                    binding.pushChip.setTextColor(
                        ContextCompat.getColor(this, R.color.chirp_text_secondary),
                    )
                    return@runOnUiThread
                }
                val registered = resp.devicesList.any { it.deviceId == chirpSession.currentDeviceId }
                binding.pushChip.setText(
                    if (registered) R.string.devices_push_on else R.string.devices_push_off,
                )
                binding.pushChip.setTextColor(
                    ContextCompat.getColor(
                        this,
                        if (registered) R.color.chirp_accent_soft else R.color.chirp_text_secondary,
                    ),
                )
            }
        }
    }

    private fun NotifItem.iconRes(): Int = when (kind) {
        NotifItem.Kind.MESSAGE -> R.drawable.ic_notif_message
        NotifItem.Kind.FRIEND -> R.drawable.ic_notif_friend
        NotifItem.Kind.PARTY -> R.drawable.ic_notif_party
        NotifItem.Kind.SYSTEM -> R.drawable.ic_notif_system
    }

    private fun NotifItem.kindRes(): Int = when (kind) {
        NotifItem.Kind.MESSAGE -> R.string.notif_kind_message
        NotifItem.Kind.FRIEND -> R.string.notif_kind_friend
        NotifItem.Kind.PARTY -> R.string.notif_kind_party
        NotifItem.Kind.SYSTEM -> R.string.notif_kind_system
    }
}
