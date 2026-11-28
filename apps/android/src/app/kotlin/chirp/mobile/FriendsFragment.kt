package chirp.mobile

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import chirp.common.Common
import chirp.game_server_gateway.GameServerGateway
import chirp.gateway.Gateway
import chirp.mobile.databinding.FragmentFriendsBinding
import chirp.mobile.databinding.ItemFriendBinding
import chirp.mobile.databinding.ItemFriendRequestBinding
import chirp.mobile.protocol.ConnState
import chirp.mobile.protocol.MsgSpecs
import chirp.social.Social
import java.util.concurrent.CompletableFuture

/**
 * Friends tab (design 03): friends + pending requests from the social
 * plane, presence from GET_PRESENCE (batched over the roster), a
 * 好友/游戏好友 segment, and the same action set as the web FriendsDialog
 * (add / accept / decline / message).
 *
 * Game-source labels from the prototype stay out: the protocol has no
 * friend-source field and the spec forbids inventing one. The game segment
 * instead shows the real self-service game-presence switch and the game ids
 * actually bound for this player.
 *
 * Refresh triggers: entering the tab, live friend/presence notifies, and
 * each action's completion. Failures surface as honest toasts — never
 * optimistic fake rows.
 */
class FriendsFragment : Fragment() {

    private var binding: FragmentFriendsBinding? = null
    private val adapter = FriendsAdapter()
    private var removeListener: (() -> Unit)? = null
    private val unsubs = mutableListOf<() -> Unit>()
    private var gameSegment = false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val b = FragmentFriendsBinding.inflate(inflater, container, false)
        binding = b
        b.friendsList.layoutManager = LinearLayoutManager(requireContext())
        b.friendsList.adapter = adapter
        return b.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val b = binding ?: return

        adapter.onMessage = { friend -> openChat(friend) }
        adapter.onAction = { requestId, accept -> answerRequest(requestId, accept) }
        b.addFriendBtn.setOnClickListener { showAddDialog() }
        b.segFriend.setOnClickListener { selectSegment(false) }
        b.segGame.setOnClickListener { selectSegment(true) }
        b.gameSwitch.setOnCheckedChangeListener { _, checked ->
            if (gameSegment) setGamePresence(checked)
        }

        removeListener = chirpSession.addListener(object : ChirpSession.Listener {
            override fun onConnectionStateChanged(state: ConnState) {
                if (state == ConnState.CONNECTED) load()
            }
        })
        wireNotifies()
        load()
    }

    private fun selectSegment(game: Boolean) {
        gameSegment = game
        val b = binding ?: return
        b.gameView.visibility = if (game) View.VISIBLE else View.GONE
        b.friendsList.visibility = if (game) View.GONE else View.VISIBLE
        b.friendsEmpty.visibility = View.GONE
        val (on, off) = if (game) b.segGame to b.segFriend else b.segFriend to b.segGame
        on.setBackgroundResource(R.drawable.bg_segment_on)
        on.setTextColor(ContextCompat.getColor(requireContext(), android.R.color.white))
        off.setBackgroundColor(android.graphics.Color.TRANSPARENT)
        off.setTextColor(ContextCompat.getColor(requireContext(), R.color.chirp_text_secondary))
        if (game) loadGamePresence() else load()
    }

    // ---- data -------------------------------------------------------------

    private fun load() {
        val session = chirpSession
        val self = session.selfId ?: return
        val friends = session.request(
            MsgSpecs.getFriendList,
            Social.GetFriendListRequest.newBuilder().setUserId(self).build(),
        )
        val pending = session.request(
            MsgSpecs.getPendingRequests,
            Social.GetPendingRequestsRequest.newBuilder().setUserId(self).build(),
        )
        friends.thenCombine(pending) { f, p -> f to p }
            .thenCompose { (f, p) ->
                // Presence ids are only known once the roster is back.
                val ok = f.code == Common.ErrorCode.OK && p.code == Common.ErrorCode.OK
                if (!ok) return@thenCompose CompletableFuture.completedFuture(f to p to null)
                val ids = f.friendsList.map { it.userId }
                if (ids.isEmpty()) return@thenCompose CompletableFuture.completedFuture(f to p to null)
                session.request(
                    MsgSpecs.getPresence,
                    Social.GetPresenceRequest.newBuilder().addAllUserIds(ids).build(),
                ).thenApply { presence -> f to p to presence }
            }
            .whenComplete { triple, err ->
                view?.post { render(triple?.first?.first, triple?.first?.second, triple?.second, err) }
            }
    }

    @Suppress("UNUSED_PARAMETER")
    private fun render(
        friendResp: Social.GetFriendListResponse?,
        pendingResp: Social.GetPendingRequestsResponse?,
        presenceResp: Social.GetPresenceResponse?,
        err: Throwable?,
    ) {
        val b = binding ?: return
        if (err != null || friendResp == null || pendingResp == null ||
            friendResp.code != Common.ErrorCode.OK || pendingResp.code != Common.ErrorCode.OK
        ) {
            adapter.submitList(emptyList())
            b.friendsEmpty.setText(R.string.friends_load_failed)
            b.friendsEmpty.visibility = if (gameSegment) View.GONE else View.VISIBLE
            return
        }
        b.friendsEmpty.setText(R.string.friends_empty)
        val self = chirpSession.selfId ?: ""
        val statusById = HashMap<String, Social.PresenceStatus>()
        presenceResp?.presencesList?.forEach { statusById[it.userId] = it.status }

        // Incoming = requests addressed to me; the rest are my outgoing asks
        // (GET_PENDING_REQUESTS may return both directions).
        val incoming = pendingResp.requestsList.filter { it.toUserId == self || it.toUserId.isEmpty() }
        val outgoing = pendingResp.requestsList.filter { it.toUserId.isNotEmpty() && it.toUserId != self }

        val rows = ArrayList<Row>()
        if (incoming.isNotEmpty()) {
            rows.add(Row.Label(getString(R.string.friends_requests, incoming.size)))
            incoming.forEach { rows.add(Row.Request(it)) }
        }
        val accepted = friendResp.friendsList.filter { it.status == Social.FriendStatus.ACCEPTED }
        if (accepted.isNotEmpty()) {
            rows.add(Row.Label(getString(R.string.friends_list, accepted.size)))
            accepted.forEach { rows.add(Row.Friend(it, statusById[it.userId])) }
        }
        val waiting = friendResp.friendsList.filter { it.status == Social.FriendStatus.PENDING }
        if (waiting.isNotEmpty()) {
            rows.add(Row.Label(getString(R.string.friends_pending_out)))
            waiting.forEach { rows.add(Row.Outgoing(it)) }
        }
        adapter.submitList(rows)
        b.friendsEmpty.visibility =
            if (rows.isEmpty() && !gameSegment) View.VISIBLE else View.GONE
    }

    private fun wireNotifies() {
        val conn = chirpSession.conn ?: return
        listOf(
            Gateway.MsgID.FRIEND_REQUEST_NOTIFY,
            Gateway.MsgID.FRIEND_ACCEPTED_NOTIFY,
            Gateway.MsgID.FRIEND_REMOVED_NOTIFY,
            Gateway.MsgID.PRESENCE_NOTIFY,
            Gateway.MsgID.FRIEND_ONLINE_NOTIFY,
        ).forEach { id ->
            unsubs.add(conn.onNotify(id) { _ -> view?.post { if (!gameSegment) load() } })
        }
    }

    /** DM shortcut: the wire channel id is the sorted pair, as everywhere. */
    private fun openChat(friend: Social.FriendInfo) {
        val self = chirpSession.selfId ?: return
        val title = friend.username.ifEmpty { friend.userId }
        val intent = Intent(requireContext(), ChatActivity::class.java)
            .putExtra(ChatActivity.EXTRA_CHANNEL_ID, listOf(self, friend.userId).sorted().joinToString("|"))
            .putExtra(ChatActivity.EXTRA_IS_GROUP, false)
            .putExtra(ChatActivity.EXTRA_TITLE, title)
        startActivity(intent)
    }

    private fun showAddDialog() {
        val input = EditText(requireContext()).apply {
            hint = getString(R.string.friends_add_target)
            setTextColor(ContextCompat.getColor(context, R.color.chirp_text_primary))
            setHintTextColor(ContextCompat.getColor(context, R.color.chirp_text_tertiary))
            background = ContextCompat.getDrawable(context, R.drawable.bg_input)
            val pad = (12 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
            singleLine = true
        }
        val frame = FrameLayout(requireContext()).apply {
            val pad = (20 * resources.displayMetrics.density).toInt()
            addView(
                input,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.WRAP_CONTENT,
                ).apply { setMargins(pad, 0, pad, 0) },
            )
        }
        AlertDialog.Builder(requireContext())
            .setTitle(R.string.friends_add_title)
            .setView(frame)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.friends_add_send) { _, _ ->
                val target = input.text.toString().trim()
                if (target.isEmpty()) return@setPositiveButton
                val self = chirpSession.selfId ?: return@setPositiveButton
                chirpSession.request(
                    MsgSpecs.addFriend,
                    Social.AddFriendRequest.newBuilder().setUserId(self).setTargetUserId(target).build(),
                ).whenComplete { resp, err ->
                    view?.post {
                        if (err == null && resp?.code == Common.ErrorCode.OK) {
                            Toast.makeText(requireContext(), R.string.friends_add_done, Toast.LENGTH_SHORT).show()
                            load()
                        } else {
                            toast(getString(R.string.friends_action_failed, failure(err, resp?.code)))
                        }
                    }
                }
            }
            .show()
    }

    private fun answerRequest(requestId: String, accept: Boolean) {
        val self = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.friendRequestAction,
            Social.FriendRequestAction.newBuilder()
                .setUserId(self).setRequestId(requestId).setAccept(accept).build(),
        ).whenComplete { resp, err ->
            view?.post {
                if (err == null && resp?.code == Common.ErrorCode.OK) {
                    load()
                } else {
                    toast(getString(R.string.friends_action_failed, failure(err, resp?.code)))
                }
            }
        }
    }

    // ---- game presence ----------------------------------------------------

    private fun loadGamePresence() {
        val player = chirpSession.selfId ?: return
        chirpSession.request(
            MsgSpecs.getGamePresence,
            GameServerGateway.GetGamePresenceRequest.newBuilder().setPlayerId(player).build(),
        ).whenComplete { resp, err ->
            view?.post {
                val b = binding ?: return@post
                if (err != null || resp?.code != Common.ErrorCode.OK) {
                    b.gameNote.setText(R.string.friends_load_failed)
                    return@post
                }
                // Assigning fires the change listener; keep gameSegment gate
                // so the echo does not write back.
                b.gameSwitch.isChecked = resp.enabled
                b.gameNote.text = when {
                    !resp.enabled -> getString(R.string.friends_game_disabled)
                    resp.entriesList.isEmpty() -> getString(R.string.friends_game_no_binding)
                    else -> getString(R.string.friends_game_enabled) + ": " +
                        resp.entriesList.joinToString(", ") { it.gameId }
                }
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
                    loadGamePresence()
                } else {
                    toast(getString(R.string.friends_action_failed, failure(err, resp?.code)))
                }
            }
        }
    }

    // ---- misc -------------------------------------------------------------

    private fun failure(err: Throwable?, code: Common.ErrorCode?): String =
        err?.message ?: code?.name ?: "?"

    private fun toast(text: String) {
        Toast.makeText(requireContext(), text, Toast.LENGTH_SHORT).show()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        removeListener?.invoke()
        removeListener = null
        unsubs.forEach { it() }
        unsubs.clear()
        binding = null
    }

    // ---- list rows --------------------------------------------------------

    private sealed interface Row {
        data class Label(val text: String) : Row
        data class Request(val request: Social.FriendRequest) : Row
        data class Friend(val friend: Social.FriendInfo, val status: Social.PresenceStatus?) : Row
        data class Outgoing(val friend: Social.FriendInfo) : Row
    }

    private class FriendsAdapter : ListAdapter<Row, RecyclerView.ViewHolder>(Diff) {

        var onMessage: ((Social.FriendInfo) -> Unit)? = null
        var onAction: ((String, Boolean) -> Unit)? = null

        override fun getItemViewType(position: Int): Int = when (getItem(position)) {
            is Row.Label -> 0
            is Row.Request -> 1
            is Row.Friend -> 2
            is Row.Outgoing -> 3
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            return when (viewType) {
                0 -> LabelHolder(
                    chirp.mobile.databinding.ItemSectionLabelBinding.inflate(inflater, parent, false),
                )
                1 -> RequestHolder(ItemFriendRequestBinding.inflate(inflater, parent, false))
                else -> FriendHolder(ItemFriendBinding.inflate(inflater, parent, false))
            }
        }

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            when (val row = getItem(position)) {
                is Row.Label -> (holder as LabelHolder).bind(row.text)
                is Row.Request -> (holder as RequestHolder).bind(row.request, onAction)
                is Row.Friend -> (holder as FriendHolder).bind(row.friend, row.status, onMessage, true)
                is Row.Outgoing -> (holder as FriendHolder).bind(row.friend, null, null, false)
            }
        }

        private class LabelHolder(
            private val binding: chirp.mobile.databinding.ItemSectionLabelBinding,
        ) : RecyclerView.ViewHolder(binding.root) {
            fun bind(text: String) {
                (binding.root as android.widget.TextView).text = text
            }
        }

        private class RequestHolder(
            private val binding: ItemFriendRequestBinding,
        ) : RecyclerView.ViewHolder(binding.root) {
            fun bind(request: Social.FriendRequest, onAction: ((String, Boolean) -> Unit)?) {
                binding.requestName.text = request.fromUserId
                binding.requestAvatar.bind(request.fromUserId)
                binding.requestAccept.setOnClickListener {
                    onAction?.invoke(request.requestId, true)
                }
                binding.requestDecline.setOnClickListener {
                    onAction?.invoke(request.requestId, false)
                }
                val actionable = request.requestId.isNotEmpty()
                binding.requestAccept.isEnabled = actionable
                binding.requestDecline.isEnabled = actionable
            }
        }

        private class FriendHolder(
            private val binding: ItemFriendBinding,
        ) : RecyclerView.ViewHolder(binding.root) {
            fun bind(
                friend: Social.FriendInfo,
                status: Social.PresenceStatus?,
                onMessage: ((Social.FriendInfo) -> Unit)?,
                accepted: Boolean,
            ) {
                val name = friend.username.ifEmpty { friend.userId }
                binding.friendName.text = name
                binding.friendAvatar.bind(name)
                binding.friendStatus.text = if (accepted) {
                    statusLabel(binding.root.context, status)
                } else {
                    binding.root.context.getString(R.string.friends_waiting)
                }
                binding.friendPresence.visibility = if (accepted) View.VISIBLE else View.GONE
                val dot = if (status == Social.PresenceStatus.ONLINE ||
                    status == Social.PresenceStatus.IN_GAME ||
                    status == Social.PresenceStatus.IN_BATTLE
                ) {
                    R.color.chirp_online
                } else {
                    R.color.chirp_text_tertiary
                }
                binding.friendPresence.background?.setTint(ContextCompat.getColor(binding.root.context, dot))
                binding.friendMessageBtn.visibility = if (accepted) View.VISIBLE else View.GONE
                binding.friendMessageBtn.setOnClickListener { onMessage?.invoke(friend) }
            }

            private fun statusLabel(context: android.content.Context, status: Social.PresenceStatus?): String {
                val res = when (status) {
                    Social.PresenceStatus.ONLINE -> R.string.friends_status_online
                    Social.PresenceStatus.AWAY -> R.string.friends_status_away
                    Social.PresenceStatus.DND -> R.string.friends_status_dnd
                    Social.PresenceStatus.IN_GAME, Social.PresenceStatus.IN_BATTLE ->
                        R.string.friends_status_in_game
                    else -> R.string.friends_status_offline
                }
                return context.getString(res)
            }
        }

        private object Diff : DiffUtil.ItemCallback<Row>() {
            override fun areItemsTheSame(oldItem: Row, newItem: Row): Boolean = when {
                oldItem is Row.Label && newItem is Row.Label -> oldItem.text == newItem.text
                oldItem is Row.Request && newItem is Row.Request ->
                    oldItem.request.requestId == newItem.request.requestId
                oldItem is Row.Friend && newItem is Row.Friend ->
                    oldItem.friend.userId == newItem.friend.userId
                oldItem is Row.Outgoing && newItem is Row.Outgoing ->
                    oldItem.friend.userId == newItem.friend.userId
                else -> false
            }

            override fun areContentsTheSame(oldItem: Row, newItem: Row): Boolean = oldItem == newItem
        }
    }
}