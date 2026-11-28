package chirp.mobile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import chirp.app_notification.AppNotification
import chirp.common.Common
import chirp.mobile.databinding.ActivityProfileBinding
import chirp.mobile.databinding.ItemDeviceBinding
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.MsgSpecs

/**
 * Profile (design 05): identity card (id copy to clipboard), the online
 * device list read-only (mutating lives in the devices tab), settings rows
 * (notification center entry, word-filter delivery state, theme statement,
 * about), and logout with a second confirmation — the same teardown path as
 * the kicked flow.
 */
class ProfileActivity : AppCompatActivity() {

    private lateinit var binding: ActivityProfileBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityProfileBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val self = chirpSession.selfId.orEmpty()
        binding.profileAvatar.bind(self)
        bindPresence()
        binding.profileId.text = getString(R.string.profile_id_line, self)

        binding.profileCopy.setOnClickListener {
            val cm = getSystemService(ClipboardManager::class.java)
            cm.setPrimaryClip(ClipData.newPlainText("chirp id", self))
            Toast.makeText(this, R.string.profile_copied, Toast.LENGTH_SHORT).show()
        }

        binding.rowNotifications.setOnClickListener {
            startActivity(Intent(this, NotificationsActivity::class.java))
        }
        binding.logoutBtn.setOnClickListener { confirmLogout() }
        bindUnreadChip()
        bindFilterChip()
        bindAboutChip()
        loadDevices()
    }

    private fun bindPresence() {
        // Same rule as the home shell subtitle: green while connected.
        val connected = chirpSession.conn?.state == ConnState.CONNECTED
        binding.profilePresence.background?.setTint(
            ContextCompat.getColor(
                this,
                if (connected) R.color.chirp_online else R.color.chirp_text_tertiary,
            ),
        )
    }

    private fun bindUnreadChip() {
        val unread = chirpSession.feedUnreadCount()
        if (unread <= 0) {
            binding.notifUnreadChip.visibility = View.GONE
        } else {
            binding.notifUnreadChip.visibility = View.VISIBLE
            binding.notifUnreadChip.text = getString(R.string.profile_unread_chip, unread)
        }
    }

    private fun bindFilterChip() {
        // WordFilterSync is server-driven: the chip states what actually came
        // down, there is no client-side switch to fake.
        val words = chirpSession.wordFilterSync?.wordCount ?: 0
        binding.filterChip.text =
            if (words > 0) getString(R.string.profile_filter_on, words)
            else getString(R.string.profile_filter_off)
    }

    private fun bindAboutChip() {
        val version = runCatching {
            packageManager.getPackageInfo(packageName, 0).versionName
        }.getOrNull() ?: "?"
        binding.aboutChip.text = getString(R.string.profile_version_chip, version)
    }

    private fun loadDevices() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getUserDevices,
            AppNotification.GetUserDevicesRequest.newBuilder().setUserId(self).build(),
        ).whenComplete { resp, err ->
            runOnUiThread {
                if (err != null || resp?.code != Common.ErrorCode.OK) {
                    binding.devicesLabel.setText(R.string.devices_service_unavailable)
                    binding.devicesList.removeAllViews()
                    return@runOnUiThread
                }
                val mine = chirpSession.currentDeviceId
                binding.devicesLabel.text =
                    getString(R.string.profile_devices_label, resp.devicesList.size)
                binding.devicesList.removeAllViews()
                resp.devicesList.forEach { info ->
                    val row = ItemDeviceBinding.inflate(layoutInflater, binding.devicesList, false)
                    row.deviceName.text = info.deviceName.ifEmpty { info.deviceId }
                    if (info.deviceId == mine) {
                        row.deviceSelfChip.visibility = View.VISIBLE
                    }
                    row.devicePlatform.text = info.platform
                    row.devicePreview.text = getString(
                        R.string.profile_device_preview,
                        info.platform,
                        getString(
                            if (info.isActive) R.string.devices_online else R.string.devices_offline,
                        ),
                    )
                    row.devicePresence.background?.setTint(
                        ContextCompat.getColor(
                            this,
                            if (info.isActive) R.color.chirp_online else R.color.chirp_text_tertiary,
                        ),
                    )
                    row.deviceRemove.visibility = View.GONE
                    binding.devicesList.addView(row.root)
                }
            }
        }
    }

    private fun confirmLogout() {
        AlertDialog.Builder(this)
            .setMessage(R.string.profile_logout_confirm)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.profile_logout) { _, _ ->
                chirpSession.logout()
                startActivity(
                    Intent(this, LoginActivity::class.java)
                        .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK),
                )
                finish()
            }
            .show()
    }
}
