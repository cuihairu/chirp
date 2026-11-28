package chirp.mobile

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import chirp.mobile.databinding.FragmentSessionsBinding
import chirp.mobile.databinding.ItemConversationBinding
import chirp.mobile.databinding.ItemSectionLabelBinding
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Sessions tab (design 01): conversations grouped into 私聊 and 群聊
 * sections, most recent first, unread pills on the right. Rows open the
 * chat screen when it lands (next commit). Content comes from the live
 * conversation registry — an empty registry shows the empty state, never
 * fake rows.
 */
class SessionsFragment : Fragment() {

    private var binding: FragmentSessionsBinding? = null
    private val adapter = SessionsAdapter()
    private var removeListener: (() -> Unit)? = null

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val b = FragmentSessionsBinding.inflate(inflater, container, false)
        binding = b
        b.sessionsList.layoutManager = LinearLayoutManager(requireContext())
        b.sessionsList.adapter = adapter
        return b.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        refresh()
        removeListener = chirpSession.addListener(object : ChirpSession.Listener {
            override fun onConversationsChanged() = refresh()
        })
    }

    private fun refresh() {
        val b = binding ?: return
        val rows = buildRows(chirpSession.conversations())
        adapter.submitList(rows)
        b.sessionsEmpty.visibility = if (rows.isEmpty()) View.VISIBLE else View.GONE
    }

    /** Two labeled sections (DMs then groups); a section hides while empty. */
    private fun buildRows(convs: List<Conversation>): List<Row> {
        val rows = ArrayList<Row>(convs.size + 2)
        val dms = convs.filter { !it.isGroup }
        val groups = convs.filter { it.isGroup }
        if (dms.isNotEmpty()) {
            rows.add(Row.Section(getString(R.string.section_dm)))
            dms.forEach { rows.add(Row.Conv(it)) }
        }
        if (groups.isNotEmpty()) {
            rows.add(Row.Section(getString(R.string.section_group)))
            groups.forEach { rows.add(Row.Conv(it)) }
        }
        return rows
    }

    override fun onDestroyView() {
        super.onDestroyView()
        removeListener?.invoke()
        removeListener = null
        binding = null
    }

    private sealed interface Row {
        data class Section(val title: String) : Row
        data class Conv(val conversation: Conversation) : Row
    }

    private class SessionsAdapter : ListAdapter<Row, RecyclerView.ViewHolder>(Diff) {

        override fun getItemViewType(position: Int): Int = when (getItem(position)) {
            is Row.Section -> 0
            is Row.Conv -> 1
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            return if (viewType == 0) {
                SectionHolder(ItemSectionLabelBinding.inflate(inflater, parent, false))
            } else {
                ConvHolder(ItemConversationBinding.inflate(inflater, parent, false))
            }
        }

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            when (val row = getItem(position)) {
                is Row.Section -> (holder as SectionHolder).bind(row.title)
                is Row.Conv -> (holder as ConvHolder).bind(row.conversation)
            }
        }

        private class SectionHolder(
            private val binding: ItemSectionLabelBinding,
        ) : RecyclerView.ViewHolder(binding.root) {
            fun bind(title: String) {
                (binding.root as android.widget.TextView).text = title
            }
        }

        private class ConvHolder(
            private val binding: ItemConversationBinding,
        ) : RecyclerView.ViewHolder(binding.root) {
            private val todayFormat = DateTimeFormatter.ofPattern("HH:mm")
            private val otherDayFormat = DateTimeFormatter.ofPattern("MM-dd")

            fun bind(conv: Conversation) {
                binding.convName.text = conv.title
                binding.convPreview.text = conv.lastPreview
                binding.convAvatar.bind(conv.title)
                val zone = ZoneId.systemDefault()
                val day = Instant.ofEpochMilli(conv.lastTimestamp).atZone(zone).toLocalDate()
                binding.convTime.text = if (day == LocalDate.now(zone)) {
                    todayFormat.format(day)
                } else {
                    otherDayFormat.format(day)
                }
                if (conv.unread > 0) {
                    binding.convUnread.visibility = View.VISIBLE
                    binding.convUnread.text = if (conv.unread > 99) "99+" else conv.unread.toString()
                } else {
                    binding.convUnread.visibility = View.GONE
                }
            }
        }

        private object Diff : DiffUtil.ItemCallback<Row>() {
            override fun areItemsTheSame(oldItem: Row, newItem: Row): Boolean = when {
                oldItem is Row.Section && newItem is Row.Section ->
                    oldItem.title == newItem.title
                oldItem is Row.Conv && newItem is Row.Conv ->
                    oldItem.conversation.key == newItem.conversation.key
                else -> false
            }

            override fun areContentsTheSame(oldItem: Row, newItem: Row): Boolean = oldItem == newItem
        }
    }
}
