package chirp.mobile

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import chirp.mobile.databinding.ActivityHomeBinding
import chirp.mobile.protocol.ConnState

/**
 * Home shell (design 01): top bar with brand mark, self id and connection
 * subtitle, five-tab bottom navigation over a fragment host. 组队/语音 share
 * one screen per the design; every tab carries its real screen now.
 */
class HomeActivity : AppCompatActivity() {

    private lateinit var binding: ActivityHomeBinding
    private var removeListener: (() -> Unit)? = null

    private val sessionListener = object : ChirpSession.Listener {
        override fun onConnectionStateChanged(state: ConnState) {
            bindSubtitle()
        }

        override fun onNotificationFeedChanged(unread: Int) {
            bindBellBadge(unread)
        }

        override fun onKicked(reason: String) {
            // Same contract as the old console: kicked → session torn down,
            // back to the login screen.
            chirpSession.logout()
            if (!isFinishing && !isDestroyed) {
                startActivity(Intent(this@HomeActivity, LoginActivity::class.java))
                finish()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityHomeBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.bottomNav.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_chat -> show(SessionsFragment())
                R.id.nav_friends -> show(FriendsFragment())
                R.id.nav_party -> show(PartyVoiceFragment.newInstance(voice = false))
                R.id.nav_voice -> show(PartyVoiceFragment.newInstance(voice = true))
                else -> show(DevicesFragment())
            }
            true
        }
        show(SessionsFragment()) // initial tab matches the checked nav item
        when (intent.getStringExtra(EXTRA_TAB)) {
            TAB_FRIENDS -> binding.bottomNav.selectedItemId = R.id.nav_friends
            TAB_PARTY -> binding.bottomNav.selectedItemId = R.id.nav_party
        }

        // Top-bar entries (design 01): bell → notification center,
        // avatar → profile. Neither occupies a tab.
        binding.bellEntry.setOnClickListener {
            startActivity(Intent(this, NotificationsActivity::class.java))
        }
        binding.avatarEntry.bind(chirpSession.selfId ?: "")
        binding.avatarEntry.setOnClickListener {
            startActivity(Intent(this, ProfileActivity::class.java))
        }

        removeListener = chirpSession.addListener(sessionListener)
        bindSubtitle()
        bindBellBadge(chirpSession.feedUnreadCount())
    }

    private fun bindBellBadge(unread: Int) {
        if (unread <= 0) {
            binding.bellBadge.visibility = View.GONE
        } else {
            binding.bellBadge.visibility = View.VISIBLE
            binding.bellBadge.text = if (unread > 9) "9+" else unread.toString()
        }
    }

    private fun show(fragment: Fragment) {
        supportFragmentManager.beginTransaction()
            .replace(R.id.fragment_host, fragment)
            .commit()
    }

    private fun bindSubtitle() {
        val self = chirpSession.selfId ?: getString(R.string.app_name)
        binding.homeSubtitle.text = if (chirpSession.conn?.state == ConnState.CONNECTED) {
            getString(R.string.home_subtitle_online, self)
        } else {
            getString(R.string.home_subtitle_offline, self)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        removeListener?.invoke()
        removeListener = null
    }

    companion object {
        const val EXTRA_TAB = "tab"
        const val TAB_FRIENDS = "friends"
        const val TAB_PARTY = "party"
    }
}
