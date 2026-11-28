package chirp.mobile

import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import chirp.chat.Chat
import chirp.common.Common
import chirp.mobile.databinding.ActivityChatBinding
import chirp.mobile.databinding.ItemMessageMineBinding
import chirp.mobile.databinding.ItemMessageTheirsBinding
import chirp.mobile.protocol.RequestError
import chirp.mobile.protocol.SendOptions
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Chat window (design 02): bubbles (mine = accent right, theirs = surface
 * left), history from the in-process archive, live appends through the
 * session listener, read-marking on open and while visible. Offline sends
 * queue and replay on reconnect — the console's wire behavior, kept.
 *
 * Wire markRead and a read-tracking store land together later: today the
 * read mark is local-only (MemoryMessageStore reports 0 unread, like the
 * dart reference), so the sessions-list pill stays honest rather than
 * claiming server-side receipts. Presence subtitle and per-message
 * interactions (reply/react/edit) land with their own commits.
 */
class ChatActivity : AppCompatActivity() {

    private lateinit var binding: ActivityChatBinding
    private val messages = ArrayList<Chat.ChatMessage>()
    private val adapter = MessageAdapter()
    private var removeListener: (() -> Unit)? = null

    private val channelId: String by lazy { intent.getStringExtra(EXTRA_CHANNEL_ID) ?: "" }
    private val isGroup: Boolean by lazy { intent.getBooleanExtra(EXTRA_IS_GROUP, false) }
    private val conversationTitle: String by lazy { intent.getStringExtra(EXTRA_TITLE) ?: "" }

    private val channelType: Chat.ChannelType
        get() = if (isGroup) Chat.ChannelType.GUILD else Chat.ChannelType.PRIVATE

    private val session: ChirpSession get() = chirpSession

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityChatBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.chatTitle.text = conversationTitle
        binding.chatAvatar.bind(conversationTitle)
        binding.backBtn.setOnClickListener { finish() }

        val layout = LinearLayoutManager(this)
        layout.stackFromEnd = true // newest at the bottom, list opens scrolled
        binding.messagesList.layoutManager = layout
        binding.messagesList.adapter = adapter

        binding.sendBtn.setOnClickListener { send() }

        // Session gone (kicked elsewhere / process reuse): the home shell
        // owns re-login, nothing to show here.
        if (session.selfId == null) {
            finish()
        }
    }

    override fun onResume() {
        super.onResume()
        reload()
        removeListener = session.addListener(object : ChirpSession.Listener {
            override fun onMessageReceived(message: Chat.ChatMessage) {
                if (message.channelId != channelId ||
                    message.channelTypeValue != channelType.number
                ) {
                    return // other conversations keep feeding the sessions list
                }
                messages.add(message)
                adapter.notifyItemInserted(messages.size - 1)
                binding.messagesList.scrollToPosition(messages.size - 1)
                markNewestRead()
            }
        })
    }

    override fun onPause() {
        super.onPause()
        removeListener?.invoke()
        removeListener = null
    }

    private fun reload() {
        messages.clear()
        // Store loads are newest-first; the list renders oldest→newest.
        val loaded = session.pipeline?.loadHistory(channelType, channelId, HISTORY_LIMIT)
            .orEmpty()
            .reversed()
        messages.addAll(loaded)
        adapter.notifyDataSetChanged()
        if (messages.isNotEmpty()) {
            binding.messagesList.scrollToPosition(messages.size - 1)
        }
        markNewestRead()
    }

    private fun markNewestRead() {
        val last = messages.lastOrNull() ?: return
        if (last.messageId.isNotEmpty()) {
            session.markRead(channelType, channelId, last.messageId)
        }
    }

    private fun send() {
        val content = binding.messageInput.text.toString().trim()
        if (content.isEmpty()) return
        val self = session.selfId ?: return
        val options = if (isGroup) {
            SendOptions(Chat.ChannelType.GUILD, channelId = channelId)
        } else {
            SendOptions(Chat.ChannelType.PRIVATE, receiverId = conversationTitle)
        }
        binding.messageInput.setText("")
        val clientId = "$self:${content.hashCode()}"
        val sent = session.send(options, content)
        if (sent == null) {
            binding.messageInput.setText(content)
            return
        }
        sent.whenComplete { resp, err ->
            runOnUiThread {
                when {
                    err is RequestError && err.kind == RequestError.Kind.CLOSED -> {
                        // Dead link: offline queue replays on reconnect.
                        session.offlineQueue?.enqueue(clientId, options, content)
                        session.noteLocalSend(channelType, channelId, content)
                        toast(getString(R.string.chat_error_queued))
                    }
                    err != null -> fail(content)
                    resp.code == Common.ErrorCode.OK -> {
                        // Pipeline archived its own copy; refresh from it and
                        // mirror the preview into the sessions registry.
                        session.noteLocalSend(channelType, channelId, content)
                        reload()
                    }
                    else -> fail(content)
                }
            }
        }
    }

    private fun fail(content: String) {
        binding.messageInput.setText(content) // give the draft back
        toast(getString(R.string.chat_error_send_failed))
    }

    private fun toast(text: String) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
    }

    private inner class MessageAdapter : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

        override fun getItemCount(): Int = messages.size

        override fun getItemViewType(position: Int): Int =
            if (messages[position].senderId == session.selfId) 0 else 1

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            return if (viewType == 0) {
                MineHolder(ItemMessageMineBinding.inflate(inflater, parent, false))
            } else {
                TheirsHolder(ItemMessageTheirsBinding.inflate(inflater, parent, false))
            }
        }

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            val message = messages[position]
            when (holder) {
                is MineHolder -> holder.bind(message)
                is TheirsHolder -> holder.bind(message, isGroup)
            }
        }
    }

    private class MineHolder(
        private val binding: ItemMessageMineBinding,
    ) : RecyclerView.ViewHolder(binding.root) {
        private val timeFormat = DateTimeFormatter.ofPattern("HH:mm")
        private val zone: ZoneId = ZoneId.systemDefault()

        fun bind(message: Chat.ChatMessage) {
            binding.msgText.text = message.content.toStringUtf8()
            binding.msgTime.text = timeFormat.format(Instant.ofEpochMilli(message.timestamp).atZone(zone))
        }
    }

    private class TheirsHolder(
        private val binding: ItemMessageTheirsBinding,
    ) : RecyclerView.ViewHolder(binding.root) {
        private val timeFormat = DateTimeFormatter.ofPattern("HH:mm")
        private val zone: ZoneId = ZoneId.systemDefault()

        fun bind(message: Chat.ChatMessage, group: Boolean) {
            binding.msgSender.visibility = if (group) ViewGroup.VISIBLE else ViewGroup.GONE
            binding.msgSender.text = message.senderId
            binding.msgText.text = message.content.toStringUtf8()
            binding.msgTime.text = timeFormat.format(Instant.ofEpochMilli(message.timestamp).atZone(zone))
        }
    }

    companion object {
        const val EXTRA_CHANNEL_ID = "channelId"
        const val EXTRA_IS_GROUP = "isGroup"
        const val EXTRA_TITLE = "title"
        private const val HISTORY_LIMIT = 50
    }
}
