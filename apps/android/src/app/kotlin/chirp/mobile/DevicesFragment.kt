package chirp.mobile

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import chirp.app_notification.AppNotification
import chirp.common.Common
import chirp.game_server_gateway.GameServerGateway
import chirp.mobile.databinding.FragmentDevicesBinding
import chirp.mobile.databinding.ItemDeviceBinding
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.MsgSpecs
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Devices tab (design 09): the self row (push registration state from
 * GET_USER_DEVICES matched against this install's persisted device id),
 * other endpoints with a confirmed unregister, the same-type-end take-over
 * contract note, and the game-presence preference backed by the real
 * GET/SET_GAME_PRESENCE_ENABLED pair.
 *
 * Unregister kicks that endpoint's session: it is terminal and confirmed
 * before firing. Push delivery is currently log-only on the server — the
 * note says so instead of promising vendor push.
 */
class DevicesFragment : Fragment() {

    private var binding: FragmentDevicesBinding? = null
    private var suppressSwitch = false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val b = FragmentDevicesBinding.inflate(inflater, container, false)
        binding = b
        return b.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val b = binding ?: return
        b.gamePresenceSwitch.setOnCheckedChangeListener { _, checked ->
            if (suppressSwitch) return@setOnCheckedChangeListener
            setGamePresence(checked)
        }
        removeListener = chirpSession.addListener(object : ChirpSession.Listener {
            override fun onConnectionStateChanged(state: ConnState) {
                if (state == ConnState.CONNECTED) load()
            }
        })
        load()
    }

    private var removeListener: (() -> Unit)? = null

    override fun onDestroyView() {
        super.onDestroyView()
        removeListener?.invoke()
        removeListener = null
        binding = null
    }

    // ---- data -------------------------------------------------------------

    private fun load() {
        loadDevices()
        loadGamePresence()
    }

    private fun loadDevices() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getUserDevices,
            AppNotification.GetUserDevicesRequest.newBuilder().setUserId(self).build(),
        ).whenComplete { resp, err ->
            view?.post {
                val b = binding ?: return@post
                if (err != null || resp?.code != Common.ErrorCode.OK) {
                    b.selfPreview.setText(R.string.devices_service_unavailable)
                    b.devicesEmpty.setText(R.string.devices_service_unavailable)
                    b.devicesList.removeAllViews()
                    return@post
                }
                val mine = chirpSession.currentDeviceId
                renderSelf(resp.devicesList.firstOrNull { it.deviceId == mine })
                val others = resp.devicesList.filter { it.deviceId != mine }
                b.devicesList.removeAllViews()
                others.forEach { info ->
                    val row = ItemDeviceBinding.inflate(layoutInflater, b.devicesList, false)
                    row.deviceName.text = info.deviceName.ifEmpty { info.deviceId }
                    row.devicePlatform.text = getString(R.string.devices_platform_fmt, info.platform)
                    row.devicePreview.text = getString(
                        R.string.devices_row_preview,
                        getString(if (info.isActive) R.string.devices_online else R.string.devices_offline),
                        formatRegistered(info.registeredAt),
                    )
                    row.devicePresence.background?.setTint(
                        ContextCompat.getColor(
                            requireContext(),
                            if (info.isActive) R.color.chirp_online else R.color.chirp_text_tertiary,
                        ),
                    )
                    row.deviceRemove.setOnClickListener { confirmRemove(info) }
                    b.devicesList.addView(row.root)
                }
                b.devicesEmpty.visibility = if (others.isEmpty()) View.VISIBLE else View.GONE
            }
        }
    }

    private fun renderSelf(info: AppNotification.DeviceInfo?) {
        val b = binding ?: return
        val self = chirpSession.selfId.orEmpty()
        b.selfAvatar.bind(self)
        b.selfName.text = info?.deviceName?.ifEmpty { android.os.Build.MODEL }
            ?: android.os.Build.MODEL
        b.selfPreview.text = if (info == null) {
            getString(R.string.devices_not_registered_hint)
        } else {
            getString(R.string.devices_registered_at, formatRegistered(info.registeredAt))
        }
        b.selfChip.text = getString(
            if (info == null) R.string.devices_push_off else R.string.devices_push_on,
        )
        b.selfChip.setTextColor(
            ContextCompat.getColor(
                requireContext(),
                if (info == null) R.color.chirp_text_secondary else R.color.chirp_accent_soft,
            ),
        )
    }

    private fun formatRegistered(epochMs: Long): String = if (epochMs <= 0) {
        getString(R.string.devices_time_unknown)
    } else {
        DateTimeFormatter.ofPattern("MM-dd HH:mm")
            .format(Instant.ofEpochMilli(epochMs).atZone(ZoneId.systemDefault()))
    }

    private fun loadGamePresence() {
        val player = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getGamePresence,
            GameServerGateway.GetGamePresenceRequest.newBuilder().setPlayerId(player).build(),
        ).whenComplete { resp, err ->
            view?.post {
                val b = binding ?: return@post
                if (err != null || resp?.code != Common.ErrorCode.OK) {
                    toast(getString(R.string.devices_pref_failed, failure(err, resp?.code)))
                    return@post
                }
                suppressSwitch = true
                b.gamePresenceSwitch.isChecked = resp.enabled
                suppressSwitch = false
            }
        }
    }

    private fun setGamePresence(enabled: Boolean) {
        val player = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.setGamePresenceEnabled,
            GameServerGateway.SetGamePresenceEnabledRequest.newBuilder()
                .setPlayerId(player).setEnabled(enabled).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) {
                    toast(getString(R.string.devices_pref_saved))
                } else {
                    toast(getString(R.string.devices_pref_failed, failure(err, resp?.code)))
                    loadGamePresence() // snap back to served truth
                }
            }
        }
    }

    private fun confirmRemove(info: AppNotification.DeviceInfo) {
        val self = chirpSession.selfId ?: return
        AlertDialog.Builder(requireContext())
            .setTitle(getString(R.string.devices_remove_title, info.deviceName.ifEmpty { info.deviceId }))
            .setMessage(R.string.devices_remove_confirm)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.devices_remove) { _, _ ->
                chirpSession.request(
                    MsgSpecs.unregisterDevice,
                    AppNotification.UnregisterDeviceRequest.newBuilder()
                        .setUserId(self).setDeviceId(info.deviceId).build(),
                ).whenComplete { resp, err ->
                    view?.post {
                        if (err == null && resp?.code == Common.ErrorCode.OK) {
                            toast(getString(R.string.devices_removed))
                            loadDevices()
                        } else {
                            toast(getString(R.string.devices_action_failed, failure(err, resp?.code)))
                        }
                    }
                }
            }
            .show()
    }

    // ---- misc -------------------------------------------------------------

    private fun failure(err: Throwable?, code: Common.ErrorCode?): String =
        err?.message ?: code?.name ?: "?"

    private fun toast(text: String) {
        Toast.makeText(requireContext(), text, Toast.LENGTH_SHORT).show()
    }
}
