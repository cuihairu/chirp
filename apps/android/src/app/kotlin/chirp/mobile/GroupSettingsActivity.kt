package chirp.mobile

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.RecyclerView
import chirp.chat.Chat
import chirp.common.Common
import chirp.gateway.Gateway
import chirp.mobile.databinding.ActivityGroupSettingsBinding
import chirp.mobile.databinding.ItemGroupMemberBinding
import chirp.mobile.protocol.MsgSpecs
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Group settings (design 10): group info + my alias + member list with
 * owner/muted chips + admin row. Members come from GET_GROUP_MEMBERS (which
 * carries alias and muted_until_ts), info from GET_GROUP_INFO; live changes
 * ride the alias/mute notifies. Mute and kick render only for privileged
 * callers (server still enforces; the UI just avoids dead buttons).
 *
 * Disband renders disabled with its honest note — the protocol has no group
 * disband request, and the spec forbids inventing one. TTL groups instead
 * expire by their expires_at (shown in the meta line).
 */
class GroupSettingsActivity : AppCompatActivity() {

    private lateinit var binding: ActivityGroupSettingsBinding
    private val adapter = MembersAdapter()
    private val unsubs = ArrayList<() -> Unit>()

    private val groupId: String by lazy { intent.getStringExtra(EXTRA_GROUP_ID) ?: "" }
    private val groupName: String by lazy { intent.getStringExtra(EXTRA_GROUP_NAME) ?: "" }

    private var info: Chat.GroupInfo? = null
    private var myRole: Chat.GroupMemberRole = Chat.GroupMemberRole.MEMBER
    private var myAlias: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityGroupSettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.backBtn.setOnClickListener { finish() }
        binding.membersList.layoutManager = LinearLayoutManager(this)
        binding.membersList.adapter = adapter
        binding.editAliasBtn.setOnClickListener { editAlias() }
        binding.inviteBtn.setOnClickListener { invite() }
        binding.leaveBtn.setOnClickListener { leave() }
        binding.disbandBtn.setOnClickListener { } // disabled: no wire request

        if (groupId.isEmpty() || chirpSession.selfId == null) {
            finish()
            return
        }
        load()
        wireNotifies()
    }

    private fun load() {
        val self = chirpSession.selfId ?: return
        val infoReq = chirpSession.request(
            MsgSpecs.getGroupInfo, Chat.GetGroupInfoRequest.newBuilder().setGroupId(groupId).build(),
        )
        val membersReq = chirpSession.request(
            MsgSpecs.getGroupMembers, Chat.GetGroupMembersRequest.newBuilder().setGroupId(groupId).build(),
        )
        infoReq.thenCombine(membersReq) { i, m -> i to m }.whenComplete { pair, err ->
            runOnUiThread {
                if (err != null || pair == null ||
                    pair.first.code != Common.ErrorCode.OK || pair.second.code != Common.ErrorCode.OK
                ) {
                    toast(getString(R.string.group_settings_action_failed, err?.message ?: pair?.first?.code?.name ?: "?"))
                    return@runOnUiThread
                }
                render(pair.first, pair.second, self)
            }
        }
    }

    private fun render(infoResp: Chat.GetGroupInfoResponse, members: Chat.GetGroupMembersResponse, self: String) {
        val info = infoResp.group
        this.info = info
        binding.groupName.text = info.groupName
        binding.groupAvatar.bind(info.groupName.ifEmpty { groupId })
        binding.groupMeta.text = metaLine(info, members.totalCount)
        binding.membersLabel.text = getString(R.string.group_settings_members, members.totalCount)

        val rows = members.membersList
        adapter.submitList(rows.toList())
        rows.firstOrNull { it.userId == self }?.let { me ->
            myRole = me.role
            myAlias = me.alias
            binding.myAlias.text = me.alias.ifEmpty { getString(R.string.group_settings_alias_hint) }
        }
        // Owner-only surface stays owner-only (design 10 shows 群主视角).
        binding.disbandBtn.visibility =
            if (myRole == Chat.GroupMemberRole.OWNER) View.VISIBLE else View.GONE
    }

    private fun metaLine(info: Chat.GroupInfo, memberCount: Int): String {
        val parts = ArrayList<String>()
        parts.add(getString(R.string.group_settings_members, memberCount))
        if (info.expiresAt > 0) {
            val fmt = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm")
            parts.add(fmt.format(Instant.ofEpochMilli(info.expiresAt).atZone(ZoneId.systemDefault())))
        }
        if (info.description.isNotEmpty()) parts.add(info.description)
        return parts.joinToString(" · ")
    }

    private fun wireNotifies() {
        val conn = chirpSession.conn ?: return
        unsubs.add(conn.onNotify(Gateway.MsgID.GROUP_MEMBER_ALIAS_UPDATED_NOTIFY) { body ->
            val n = try {
                Chat.GroupMemberAliasUpdatedNotify.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify
            }
            if (n.groupId != groupId) return@onNotify
            runOnUiThread {
                if (n.userId == chirpSession.selfId) {
                    myAlias = n.alias
                    binding.myAlias.text = n.alias.ifEmpty { getString(R.string.group_settings_alias_hint) }
                }
                adapter.updateAlias(n.userId, n.alias)
            }
        })
        unsubs.add(conn.onNotify(Gateway.MsgID.GROUP_MEMBER_MUTED_NOTIFY) { body ->
            val n = try {
                Chat.GroupMemberMutedNotify.parseFrom(body)
            } catch (_: Exception) {
                return@onNotify
            }
            if (n.groupId != groupId) return@onNotify
            runOnUiThread { adapter.updateMute(n.userId, n.mutedUntilTs) }
        })
        // Membership churn (join/left/kick) refreshes the whole roster.
        listOf(
            Gateway.MsgID.GROUP_MEMBER_JOINED_NOTIFY,
            Gateway.MsgID.GROUP_MEMBER_LEFT_NOTIFY,
            Gateway.MsgID.GROUP_MEMBER_KICKED_NOTIFY,
        ).forEach { id ->
            unsubs.add(conn.onNotify(id) { _ -> runOnUiThread { load() } })
        }
    }

    private fun editAlias() {
        val self = chirpSession.selfId ?: return
        val input = textField(this, getString(R.string.group_settings_alias_title), myAlias)
        AlertDialog.Builder(this)
            .setTitle(R.string.group_settings_alias_title)
            .setView(input)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val alias = input.text.toString().trim()
                chirpSession.request(
                    MsgSpecs.setMemberAlias,
                    Chat.SetMemberAliasRequest.newBuilder()
                        .setGroupId(groupId).setTargetUserId(self).setAlias(alias).build(),
                ).whenComplete { resp, err ->
                    runOnUiThread {
                        if (err == null && resp?.code == Common.ErrorCode.OK) {
                            Toast.makeText(this, R.string.group_settings_alias_saved, Toast.LENGTH_SHORT).show()
                        } else {
                            toast(getString(R.string.group_settings_action_failed, err?.message ?: resp?.code?.name ?: "?"))
                        }
                    }
                }
            }
            .show()
    }

    private fun invite() {
        val self = chirpSession.selfId ?: return
        val input = textField(this, getString(R.string.group_settings_invite), "")
        AlertDialog.Builder(this)
            .setTitle(R.string.group_settings_add_member)
            .setView(input)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.group_settings_invite) { _, _ ->
                val target = input.text.toString().trim()
                if (target.isEmpty()) return@setPositiveButton
                chirpSession.request(
                    MsgSpecs.inviteToGroup,
                    Chat.InviteToGroupRequest.newBuilder()
                        .setInviterId(self).setGroupId(groupId).setTargetUserId(target).build(),
                ).whenComplete { resp, err ->
                    runOnUiThread {
                        if (err == null && resp?.code == Common.ErrorCode.OK) load()
                        else toast(getString(R.string.group_settings_action_failed, err?.message ?: resp?.code?.name ?: "?"))
                    }
                }
            }
            .show()
    }

    private fun mute(target: Chat.GroupMember) {
        val input = textField(this, getString(R.string.group_settings_mute_title), "")
        AlertDialog.Builder(this)
            .setTitle(R.string.group_settings_mute_title)
            .setView(input)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val duration = input.text.toString().trim().toLongOrNull() ?: 0L
                chirpSession.request(
                    MsgSpecs.setGroupMute,
                    Chat.SetGroupMuteRequest.newBuilder()
                        .setGroupId(groupId).setTargetUserId(target.userId)
                        .setDurationSec(duration.coerceIn(0, 30L * 86_400L)).build(),
                ).whenComplete { resp, err ->
                    runOnUiThread {
                        if (err == null && resp?.code == Common.ErrorCode.OK) load()
                        else toast(getString(R.string.group_settings_action_failed, err?.message ?: resp?.code?.name ?: "?"))
                    }
                }
            }
            .show()
    }

    private fun kick(target: Chat.GroupMember) {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.kickMember,
            Chat.KickMemberRequest.newBuilder()
                .setRequesterId(self).setGroupId(groupId).setTargetUserId(target.userId).build(),
        ).whenComplete { resp, err ->
            runOnUiThread {
                if (err == null && resp?.code == Common.ErrorCode.OK) load()
                else toast(getString(R.string.group_settings_action_failed, err?.message ?: resp?.code?.name ?: "?"))
            }
        }
    }

    private fun leave() {
        val self = chirpSession.selfId ?: return
        AlertDialog.Builder(this)
            .setMessage(R.string.group_settings_leave)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                chirpSession.request(
                    MsgSpecs.leaveGroup,
                    Chat.LeaveGroupRequest.newBuilder().setUserId(self).setGroupId(groupId).build(),
                ).whenComplete { resp, err ->
                    runOnUiThread {
                        if (err == null && resp?.code == Common.ErrorCode.OK) finish()
                        else toast(getString(R.string.group_settings_action_failed, err?.message ?: resp?.code?.name ?: "?"))
                    }
                }
            }
            .show()
    }

    override fun onDestroy() {
        super.onDestroy()
        unsubs.forEach { it() }
        unsubs.clear()
    }

    companion object {
        const val EXTRA_GROUP_ID = "groupId"
        const val EXTRA_GROUP_NAME = "groupName"
    }
}

/**
 * Member rows. Privileged actions render for MODERATOR+ (against other
 * members); the server re-checks authority, the UI only avoids dead taps.
 */
private class MembersAdapter :
    ListAdapter<Chat.GroupMember, RecyclerView.ViewHolder>(Diff) {

    var onMute: ((Chat.GroupMember) -> Unit)? = null
    var onKick: ((Chat.GroupMember) -> Unit)? = null
    var selfId: String? = null
    var privileged: Boolean = false

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val binding = ItemGroupMemberBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return Holder(binding)
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        val member = currentList[position]
        val h = holder as Holder
        val context = h.binding.root.context
        val name = member.alias.ifEmpty { member.username.ifEmpty { member.userId } }
        h.binding.memberName.text = name
        h.binding.memberAvatar.bind(name)
        h.binding.memberOwnerChip.visibility =
            if (member.role == Chat.GroupMemberRole.OWNER) View.VISIBLE else View.GONE

        val sub = ArrayList<String>()
        if (member.username.isNotEmpty() && member.alias.isNotEmpty()) sub.add(member.username)
        if (member.mutedUntilTs > System.currentTimeMillis()) {
            sub.add(
                context.getString(
                    R.string.group_settings_muted_until,
                    java.time.format.DateTimeFormatter.ofPattern("MM-dd HH:mm")
                        .format(Instant.ofEpochMilli(member.mutedUntilTs).atZone(ZoneId.systemDefault())),
                ),
            )
        }
        h.binding.memberSub.text = sub.joinToString(" · ")

        val canAct = privileged && member.userId != selfId &&
            member.role != Chat.GroupMemberRole.OWNER
        if (canAct) {
            h.binding.memberAction.visibility = View.VISIBLE
            h.binding.memberAction.text = context.getString(R.string.group_settings_mute_title)
            h.binding.memberAction.setOnClickListener { onMute?.invoke(member) }
            h.binding.memberAction.setOnLongClickListener { onKick?.invoke(member); true }
        } else {
            h.binding.memberAction.visibility = View.GONE
            h.binding.memberAction.setOnClickListener(null)
            h.binding.memberAction.setOnLongClickListener(null)
        }
    }

    fun updateAlias(userId: String, alias: String) {
        val idx = currentList.indexOfFirst { it.userId == userId }
        if (idx < 0) return
        val updated = currentList[idx].toBuilder().setAlias(alias).build()
        submitList(currentList.toMutableList().also { it[idx] = updated })
    }

    fun updateMute(userId: String, mutedUntilTs: Long) {
        val idx = currentList.indexOfFirst { it.userId == userId }
        if (idx < 0) return
        val updated = currentList[idx].toBuilder().setMutedUntilTs(mutedUntilTs).build()
        submitList(currentList.toMutableList().also { it[idx] = updated })
    }

    class Holder(val binding: ItemGroupMemberBinding) : RecyclerView.ViewHolder(binding.root)

    object Diff : DiffUtil.ItemCallback<Chat.GroupMember>() {
        override fun areItemsTheSame(a: Chat.GroupMember, b: Chat.GroupMember) =
            a.userId == b.userId

        override fun areContentsTheSame(a: Chat.GroupMember, b: Chat.GroupMember) =
            a == b
    }
}