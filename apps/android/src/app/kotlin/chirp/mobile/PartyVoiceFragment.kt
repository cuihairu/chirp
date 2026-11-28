package chirp.mobile

import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import chirp.common.Common
import chirp.gateway.Gateway
import chirp.mobile.databinding.FragmentPartyVoiceBinding
import chirp.mobile.databinding.ItemPartyMemberBinding
import chirp.mobile.databinding.ItemVoiceParticipantBinding
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.MsgSpecs
import chirp.party.Party
import chirp.voice.Voice

/**
 * Party + voice combined plane (design 08). Party is the full control face:
 * create, invite (with the live invite card from INVITE_NOTIFY), ready
 * check, disband (leader), kick via member long-press (leader), live
 * snapshots from PARTY_STATE_CHANGED / PARTY_DISBANDED. Voice is the
 * control plane only: create/join/leave rooms, mute/deafen toggles,
 * participant state chips — the join sends an empty sdp_offer exactly like
 * the desktop/harmony clients; media negotiation is out of scope on this
 * client and the screen says so instead of faking a live call.
 *
 * The fragment fills both 组队 and 语音 nav tabs; [args] pick the initial
 * segment.
 */
class PartyVoiceFragment : Fragment() {

    private var binding: FragmentPartyVoiceBinding? = null
    private val unsubs = ArrayList<() -> Unit>()

    private var party: Party.PartyInfo? = null
    private var roomId: String? = null
    private var muted = false
    private var deafened = false
    private var pendingInvite: Party.InviteNotify? = null

    private val startWithVoice: Boolean
        get() = arguments?.getBoolean(ARG_VOICE, false) ?: false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val b = FragmentPartyVoiceBinding.inflate(inflater, container, false)
        binding = b
        return b.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val b = binding ?: return
        b.sectionTitle.setText(if (startWithVoice) R.string.tab_voice_title else R.string.tab_party_title)
        selectVoice(startWithVoice)

        b.inviteAccept.setOnClickListener { answerInvite(true) }
        b.inviteDecline.setOnClickListener { answerInvite(false) }
        b.createPartyBtn.setOnClickListener { createParty() }
        b.readySwitch.setOnCheckedChangeListener { _, checked ->
            if (suppressSwitch) return@setOnCheckedChangeListener
            setReady(checked)
        }
        b.inviteMemberBtn.setOnClickListener { inviteMember() }
        b.disbandBtn.setOnClickListener { confirmDisband() }
        b.createRoomBtn.setOnClickListener { createRoom() }
        b.joinRoomBtn.setOnClickListener { joinRoomDialog() }
        b.leaveRoomBtn.setOnClickListener { leaveRoom() }
        b.muteSwitch.setOnCheckedChangeListener { _, checked ->
            if (suppressSwitch) return@setOnCheckedChangeListener
            setMute(checked)
        }
        b.deafenSwitch.setOnCheckedChangeListener { _, checked ->
            if (suppressSwitch) return@setOnCheckedChangeListener
            setDeafen(checked)
        }

        removeListener = chirpSession.addListener(object : ChirpSession.Listener {
            override fun onConnectionStateChanged(state: ConnState) {
                if (state == ConnState.CONNECTED) reload()
            }
        })
        wireNotifies()
        reload()
    }

    private var removeListener: (() -> Unit)? = null
    /** Guards against listener echo when toggles are set programmatically. */
    private var suppressSwitch = false

    override fun onDestroyView() {
        super.onDestroyView()
        removeListener?.invoke()
        removeListener = null
        unsubs.forEach { it() }
        unsubs.clear()
        binding = null
    }

    private fun selectVoice(voice: Boolean) {
        val b = binding ?: return
        b.voiceView.visibility = if (voice) View.VISIBLE else View.GONE
        b.partyView.visibility = if (voice) View.GONE else View.VISIBLE
        b.sectionTitle.setText(if (voice) R.string.tab_voice_title else R.string.tab_party_title)
    }

    // ---- data -------------------------------------------------------------

    private fun reload() {
        loadParty()
        loadRoom()
    }

    private fun loadParty() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getMyParty,
            Party.GetMyPartyRequest.newBuilder().setUserId(self).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK && resp.inParty) {
                    renderParty(resp.party)
                } else {
                    renderParty(null)
                }
            }
        }
    }

    private fun renderParty(info: Party.PartyInfo?) {
        val b = binding ?: return
        party = info
        if (info == null || info.membersList.isEmpty()) {
            b.partyCard.visibility = View.GONE
            b.partyEmpty.visibility = View.VISIBLE
            b.createPartyBtn.visibility = View.VISIBLE
            return
        }
        val self = chirpSession.selfId ?: return
        val isLeader = info.leaderId == self
        b.partyCard.visibility = View.VISIBLE
        b.partyEmpty.visibility = View.GONE
        b.createPartyBtn.visibility = View.GONE
        b.partyHead.text = getString(
            R.string.party_head, info.partyId, info.membersCount, info.maxMembers,
        )
        b.partyMembers.removeAllViews()
        val inflater = layoutInflater
        info.membersList.forEach { member ->
            val row = ItemPartyMemberBinding.inflate(inflater, b.partyMembers, false)
            row.partyName.text = member.userId
            row.partyAvatar.bind(member.userId)
            row.partyReadyChip.text = getString(
                if (member.ready) R.string.party_ready_on else R.string.party_ready_off,
            )
            row.partyReadyChip.setTextColor(
                ContextCompat.getColor(
                    requireContext(),
                    if (member.ready) R.color.chirp_accent_soft else R.color.chirp_text_tertiary,
                ),
            )
            if (isLeader && member.userId != self) {
                // Leader-only kick, confirmed before it fires.
                row.root.setOnLongClickListener { confirmKick(member.userId); true }
            }
            b.partyMembers.addView(row.root)
        }
        suppressSwitch = true
        b.readySwitch.isChecked = info.membersList.firstOrNull { it.userId == self }?.ready ?: false
        suppressSwitch = false
        b.disbandBtn.visibility = if (isLeader) View.VISIBLE else View.GONE
    }

    private fun loadRoom() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getUserRoom,
            Voice.GetUserRoomRequest.newBuilder().setUserId(self).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK && resp.roomId.isNotEmpty()) {
                    refreshRoom(resp.roomId)
                } else {
                    renderRoom(null)
                }
            }
        }
    }

    private fun refreshRoom(id: String) {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getRoomInfo,
            Voice.GetRoomInfoRequest.newBuilder().setRoomId(id).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) {
                    renderRoom(resp)
                } else {
                    renderRoom(null)
                }
            }
        }
    }

    private fun renderRoom(info: Voice.GetRoomInfoResponse?) {
        val b = binding ?: return
        if (info == null || info.roomId.isEmpty()) {
            roomId = null
            b.roomCard.visibility = View.GONE
            b.roomEntry.visibility = View.VISIBLE
            return
        }
        roomId = info.roomId
        val self = chirpSession.selfId ?: return
        val me = info.participantsList.firstOrNull { it.userId == self }
        b.roomCard.visibility = View.VISIBLE
        b.roomEntry.visibility = View.GONE
        b.roomHead.text = getString(
            R.string.voice_room_head,
            info.roomName.ifEmpty { info.roomId },
            info.participantsCount,
        )
        b.roomParticipants.removeAllViews()
        val inflater = layoutInflater
        info.participantsList.forEach { p ->
            val row = ItemVoiceParticipantBinding.inflate(inflater, b.roomParticipants, false)
            row.voiceName.text = p.userId
            row.voiceAvatar.bind(p.userId)
            val (label, color) = participantChip(p)
            row.voiceStateChip.text = label
            row.voiceStateChip.setTextColor(ContextCompat.getColor(requireContext(), color))
            b.roomParticipants.addView(row.root)
        }
        muted = me?.muted ?: false
        deafened = me?.deafened ?: false
        suppressSwitch = true
        b.muteSwitch.isChecked = muted
        b.deafenSwitch.isChecked = deafened
        suppressSwitch = false
    }

    private fun participantChip(p: Voice.ParticipantInfo): Pair<String, Int> {
        val res = when {
            p.isSpeaking -> R.string.voice_state_speaking to R.color.chirp_online
            p.deafened -> R.string.voice_state_deafened to R.color.chirp_danger
            p.muted -> R.string.voice_state_muted to R.color.chirp_warn
            p.state == Voice.ParticipantState.JOINING -> R.string.voice_state_joining to R.color.chirp_text_tertiary
            else -> R.string.voice_state_connected to R.color.chirp_accent_soft
        }
        return getString(res.first) to res.second
    }

    // ---- party actions ----------------------------------------------------

    private fun createParty() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.createParty,
            Party.CreatePartyRequest.newBuilder().setUserId(self).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) renderParty(resp.party)
                else toastFailed(err, resp?.code)
            }
        }
    }

    private fun setReady(ready: Boolean) {
        val self = chirpSession.selfId ?: return
        val id = party?.partyId ?: return
        chirpSession.request(
            MsgSpecs.setPartyReady,
            Party.SetReadyRequest.newBuilder().setUserId(self).setPartyId(id).setReady(ready).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) loadParty()
                else {
                    toastFailed(err, resp?.code)
                    loadParty() // snap the toggle back to the served truth
                }
            }
        }
    }

    private fun inviteMember() {
        val self = chirpSession.selfId ?: return
        val id = party?.partyId ?: return
        val input = textField(requireContext(), getString(R.string.party_invite_target), "")
        AlertDialog.Builder(requireContext())
            .setTitle(R.string.party_invite)
            .setView(input)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val target = input.text.toString().trim()
                if (target.isEmpty()) return@setPositiveButton
                chirpSession.request(
                    MsgSpecs.inviteToParty,
                    Party.InviteToPartyRequest.newBuilder()
                        .setUserId(self).setPartyId(id).setTargetUserId(target).build(),
                ).whenComplete { resp, err ->
                    view?.post {
                        if (err == null && resp?.code == Common.ErrorCode.OK) {
                            Toast.makeText(requireContext(), R.string.party_invite_sent, Toast.LENGTH_SHORT).show()
                        } else {
                            toastFailed(err, resp?.code)
                        }
                    }
                }
            }
            .show()
    }

    private fun confirmDisband() {
        val self = chirpSession.selfId ?: return
        val id = party?.partyId ?: return
        AlertDialog.Builder(requireContext())
            .setMessage(R.string.party_disband_confirm)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                chirpSession.request(
                    MsgSpecs.disbandParty,
                    Party.DisbandPartyRequest.newBuilder().setUserId(self).setPartyId(id).build(),
                ).whenComplete { resp, err ->
                    view?.post {
                        if (err == null && resp?.code == Common.ErrorCode.OK) loadParty()
                        else toastFailed(err, resp?.code)
                    }
                }
            }
            .show()
    }

    private fun confirmKick(target: String) {
        val self = chirpSession.selfId ?: return
        val id = party?.partyId ?: return
        AlertDialog.Builder(requireContext())
            .setMessage(getString(R.string.party_kick_confirm, target))
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.party_kick) { _, _ ->
                chirpSession.request(
                    MsgSpecs.kickPartyMember,
                    Party.KickMemberRequest.newBuilder()
                        .setUserId(self).setPartyId(id).setTargetUserId(target).build(),
                ).whenComplete { resp, err ->
                    view?.post {
                        if (err == null && resp?.code == Common.ErrorCode.OK) loadParty()
                        else toastFailed(err, resp?.code)
                    }
                }
            }
            .show()
    }

    private fun answerInvite(accept: Boolean) {
        val self = chirpSession.selfId ?: return
        val invite = pendingInvite ?: return
        val done: (Throwable?, Common.ErrorCode?, Party.PartyInfo?) -> Unit =
            { err, code, newParty ->
                view?.post {
                    pendingInvite = null
                    binding?.inviteCard?.visibility = View.GONE
                    if (err == null && code == Common.ErrorCode.OK) {
                        newParty?.let { renderParty(it) }
                        if (newParty == null) loadParty()
                    } else {
                        toastFailed(err, code)
                    }
                }
            }
        if (accept) {
            chirpSession.request(
                MsgSpecs.acceptPartyInvite,
                Party.AcceptInviteRequest.newBuilder()
                    .setUserId(self).setInviteId(invite.inviteId).build(),
            ).whenComplete { resp, err ->
                done(err, resp?.code, if (err == null) resp?.party else null)
            }
        } else {
            chirpSession.request(
                MsgSpecs.declinePartyInvite,
                Party.DeclineInviteRequest.newBuilder()
                    .setUserId(self).setInviteId(invite.inviteId).build(),
            ).whenComplete { resp, err -> done(err, resp?.code, null) }
        }
    }

    // ---- voice actions ----------------------------------------------------

    private fun createRoom() {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.createRoom,
            Voice.CreateRoomRequest.newBuilder()
                .setUserId(self)
                .setRoomType(Voice.RoomType.GROUP)
                .build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) refreshRoom(resp.roomId)
                else toastFailed(err, resp?.code)
            }
        }
    }

    /** Empty sdp_offer = desktop parity: control-plane join, no media. */
    private fun joinRoomDialog() {
        val self = chirpSession.selfId ?: return
        val input = textField(requireContext(), getString(R.string.voice_join_target), "")
        AlertDialog.Builder(requireContext())
            .setTitle(R.string.voice_join)
            .setView(input)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val id = input.text.toString().trim()
                if (id.isEmpty()) return@setPositiveButton
                chirpSession.request(
                    MsgSpecs.joinRoom,
                    Voice.JoinRoomRequest.newBuilder()
                        .setUserId(self).setRoomId(id).setSdpOffer("").build(),
                ).whenComplete { resp, err ->
                    view?.post {
                        if (err == null && resp?.code == Common.ErrorCode.OK) refreshRoom(id)
                        else toastFailed(err, resp?.code)
                    }
                }
            }
            .show()
    }

    private fun leaveRoom() {
        val self = chirpSession.selfId ?: return
        val id = roomId ?: return
        chirpSession.request(
            MsgSpecs.leaveRoom,
            Voice.LeaveRoomRequest.newBuilder().setUserId(self).setRoomId(id).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) renderRoom(null)
                else toastFailed(err, resp?.code)
            }
        }
    }

    private fun setMute(muted: Boolean) {
        val self = chirpSession.selfId ?: return
        val id = roomId ?: return
        chirpSession.request(
            MsgSpecs.setMute,
            Voice.SetMuteRequest.newBuilder().setUserId(self).setRoomId(id).setMuted(muted).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) refreshRoom(id)
                else {
                    toastFailed(err, resp?.code)
                    refreshRoom(id)
                }
            }
        }
    }

    private fun setDeafen(deafened: Boolean) {
        val self = chirpSession.selfId ?: return
        val id = roomId ?: return
        chirpSession.request(
            MsgSpecs.setDeafen,
            Voice.SetDeafenRequest.newBuilder().setUserId(self).setRoomId(id).setDeafened(deafened).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) refreshRoom(id)
                else {
                    toastFailed(err, resp?.code)
                    refreshRoom(id)
                }
            }
        }
    }

    // ---- live notifies ----------------------------------------------------

    private fun wireNotifies() {
        val conn = chirpSession.conn ?: return
        unsubs.add(conn.onNotify(Gateway.MsgID.INVITE_NOTIFY) { body ->
            val n = try {
                Party.InviteNotify.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify
            }
            runOnUiThread {
                pendingInvite = n
                val b = binding ?: return@runOnUiThread
                b.inviteAvatar.bind(n.fromUserId)
                b.inviteText.text = getString(R.string.party_invite_card, n.fromUserId)
                b.inviteCard.visibility = View.VISIBLE
            }
        })
        unsubs.add(conn.onNotify(Gateway.MsgID.PARTY_STATE_CHANGED_NOTIFY) { _ ->
            runOnUiThread { loadParty() }
        })
        unsubs.add(conn.onNotify(Gateway.MsgID.PARTY_DISBANDED_NOTIFY) { _ ->
            runOnUiThread { renderParty(null) }
        })
        listOf(
            Gateway.MsgID.PARTICIPANT_JOINED_NOTIFY,
            Gateway.MsgID.PARTICIPANT_LEFT_NOTIFY,
            Gateway.MsgID.PARTICIPANT_STATE_CHANGED_NOTIFY,
            Gateway.MsgID.SPEAKING_NOTIFY,
        ).forEach { id ->
            unsubs.add(conn.onNotify(id) { _ ->
                roomId?.let { runOnUiThread { refreshRoom(it) } }
            })
        }
    }

    // ---- misc -------------------------------------------------------------

    private fun toastFailed(err: Throwable?, code: Common.ErrorCode?) {
        Toast.makeText(
            requireContext(),
            getString(R.string.action_failed, err?.message ?: code?.name ?: "?"),
            Toast.LENGTH_SHORT,
        ).show()
    }

    companion object {
        const val ARG_VOICE = "voice"

        fun newInstance(voice: Boolean): PartyVoiceFragment =
            PartyVoiceFragment().apply {
                arguments = Bundle().apply { putBoolean(ARG_VOICE, voice) }
            }
    }
}