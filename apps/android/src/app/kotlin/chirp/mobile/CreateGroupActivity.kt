package chirp.mobile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import chirp.chat.Chat
import chirp.common.Common
import chirp.mobile.databinding.ActivityCreateGroupBinding
import chirp.mobile.protocol.MsgSpecs

/**
 * Create group screen (design 07): name, description, TTL segment
 * (permanent / 1 day / 7 days / 30 days — the wire ttl_seconds field), and
 * initial members by user id as removable chips. The 30-day ceiling is a
 * protocol rule (the server rejects beyond it with INVALID_PARAM).
 *
 * On success the new group id lands on the clipboard and the group chat
 * opens directly — the sessions registry is traffic-fed, so without this
 * the fresh group would be invisible until the first message.
 */
class CreateGroupActivity : AppCompatActivity() {

    private lateinit var binding: ActivityCreateGroupBinding
    private val members = ArrayList<String>()
    private var ttlSeconds: Long = 0
    private val ttlButtons = ArrayList<Button>()
    private var submitting = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityCreateGroupBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.backBtn.setOnClickListener { finish() }

        ttlButtons.addAll(listOf(binding.ttlForever, binding.ttlDay, binding.ttlWeek, binding.ttlMonth))
        selectTtl(0, binding.ttlForever)
        binding.ttlForever.setOnClickListener { selectTtl(0, binding.ttlForever) }
        binding.ttlDay.setOnClickListener { selectTtl(SECONDS_PER_DAY, binding.ttlDay) }
        binding.ttlWeek.setOnClickListener { selectTtl(7 * SECONDS_PER_DAY, binding.ttlWeek) }
        binding.ttlMonth.setOnClickListener { selectTtl(30 * SECONDS_PER_DAY, binding.ttlMonth) }

        binding.addMemberBtn.setOnClickListener { addMember() }
        binding.groupName.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                binding.groupError.visibility = View.GONE
            }

            override fun afterTextChanged(s: Editable?) {}
        })
        binding.createBtn.setOnClickListener { create() }
    }

    private fun selectTtl(seconds: Long, selected: Button) {
        ttlSeconds = seconds
        ttlButtons.forEach { button ->
            val on = button === selected
            button.background = ContextCompat.getDrawable(
                this, if (on) R.drawable.bg_segment_on else android.R.color.transparent,
            )
            button.setTextColor(
                ContextCompat.getColor(
                    this, if (on) android.R.color.white else R.color.chirp_text_secondary,
                ),
            )
        }
    }

    private fun addMember() {
        val id = binding.memberInput.text.toString().trim()
        if (id.isEmpty()) return
        binding.memberInput.setText("")
        if (members.contains(id) || members.size >= MAX_INITIAL_MEMBERS) return
        members.add(id)
        binding.memberChips.addView(chipView(id))
    }

    private fun chipView(id: String): LinearLayout {
        val pad = { v: Int -> (v * resources.displayMetrics.density).toInt() }
        return LinearLayout(this).apply {
            gravity = Gravity.CENTER
            background = ContextCompat.getDrawable(this@CreateGroupActivity, R.drawable.bg_chip)
            setPadding(pad(10), pad(5), pad(6), pad(5))
            addView(TextView(this@CreateGroupActivity).apply {
                text = id
                setTextColor(ContextCompat.getColor(context, R.color.chirp_text_primary))
                textSize = 12f
            })
            addView(Button(this@CreateGroupActivity).apply {
                text = "×"
                setTextColor(ContextCompat.getColor(context, R.color.chirp_text_secondary))
                background = null
                minWidth = 0
                minimumWidth = 0
                setPadding(pad(4), 0, pad(4), 0)
                setOnClickListener {
                    members.remove(id)
                    (parent as? LinearLayout)?.removeView(this)
                }
            })
        }
    }

    private fun create() {
        if (submitting) return
        val name = binding.groupName.text.toString().trim()
        if (name.isEmpty()) {
            binding.groupError.text = getString(R.string.group_error_name)
            binding.groupError.visibility = View.VISIBLE
            return
        }
        val self = chirpSession.selfId ?: return
        submitting = true
        binding.createBtn.isEnabled = false
        binding.createBtn.setText(R.string.login_btn_in_progress)
        binding.groupError.visibility = View.GONE
        chirpSession.request(
            MsgSpecs.createGroup,
            Chat.CreateGroupRequest.newBuilder()
                .setCreatorId(self)
                .setGroupName(name)
                .setDescription(binding.groupDesc.text.toString().trim())
                .addAllInitialMembers(members)
                .setTtlSeconds(ttlSeconds)
                .build(),
        ).whenComplete { resp, err ->
            runOnUiThread {
                submitting = false
                binding.createBtn.isEnabled = true
                binding.createBtn.setText(R.string.group_create_btn)
                when {
                    err != null -> fail(getString(R.string.group_error_create, err.message ?: "?"))
                    resp == null || resp.code != Common.ErrorCode.OK ->
                        fail(getString(R.string.group_error_create, resp?.code?.name ?: "?"))
                    else -> {
                        copyToClipboard(resp.groupId)
                        Toast.makeText(
                            this,
                            getString(R.string.group_created, resp.groupId),
                            Toast.LENGTH_LONG,
                        ).show()
                        // Straight into the new group chat; the id travels on
                        // the clipboard for inviting people by hand.
                        startActivity(
                            Intent(this, ChatActivity::class.java)
                                .putExtra(ChatActivity.EXTRA_CHANNEL_ID, resp.groupId)
                                .putExtra(ChatActivity.EXTRA_IS_GROUP, true)
                                .putExtra(ChatActivity.EXTRA_TITLE, name),
                        )
                        finish()
                    }
                }
            }
        }
    }

    private fun fail(message: String) {
        binding.groupError.text = message
        binding.groupError.visibility = View.VISIBLE
    }

    private fun copyToClipboard(text: String) {
        getSystemService(ClipboardManager::class.java)
            ?.setPrimaryClip(ClipData.newPlainText("group_id", text))
    }

    companion object {
        private const val SECONDS_PER_DAY = 86_400L
        private const val MAX_INITIAL_MEMBERS = 20
    }
}