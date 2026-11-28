package chirp.mobile

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import chirp.common.Common
import chirp.mobile.databinding.ActivityLoginBinding

/**
 * Login screen (design 06): brand mark, server base address (the harmony
 * HostPage semantics folded into one screen — first launch expands it),
 * user id, primary pill button. Dev mode: the user id IS the scaffold token;
 * no password field exists until real credential auth lands (do not fake
 * one).
 *
 * Server default is the emulator host-loopback alias (same as the M4 console
 * dev default); the value persists per install once a login succeeds.
 */
class LoginActivity : AppCompatActivity() {

    private lateinit var binding: ActivityLoginBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityLoginBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val session = chirpSession
        val prefs = getSharedPreferences(SESSION_PREFS, MODE_PRIVATE)
        binding.inputServer.setText(prefs.getString(KEY_HOST, DEFAULT_HOST))
        binding.inputUser.setText(prefs.getString(KEY_USER, ""))

        // Already logged in (process kept the session): skip straight in.
        if (session.selfId != null) {
            openConsole()
            return
        }

        binding.loginBtn.setOnClickListener { submit() }
    }

    private fun submit() {
        val session = chirpSession
        val host = binding.inputServer.text.toString().trim()
        val user = binding.inputUser.text.toString().trim()

        var valid = true
        if (!host.startsWith("ws://") && !host.startsWith("wss://")) {
            binding.fieldServer.error = getString(R.string.login_error_host)
            valid = false
        } else {
            binding.fieldServer.error = null
        }
        if (user.isEmpty()) {
            binding.fieldUser.error = getString(R.string.login_error_user)
            valid = false
        } else {
            binding.fieldUser.error = null
        }
        if (!valid) return

        binding.loginBtn.isEnabled = false
        binding.loginBtn.setText(R.string.login_btn_in_progress)
        binding.loginError.visibility = View.GONE

        session.login(host, user).whenComplete { code, err ->
            runOnUiThread {
                when {
                    err != null -> fail(getString(R.string.login_error_network))
                    code != Common.ErrorCode.OK -> fail(getString(R.string.login_error_rejected, code))
                    else -> {
                        getSharedPreferences(SESSION_PREFS, MODE_PRIVATE).edit()
                            .putString(KEY_HOST, host)
                            .putString(KEY_USER, user)
                            .apply()
                        openConsole()
                    }
                }
            }
        }
    }

    private fun fail(message: String) {
        binding.loginBtn.isEnabled = true
        binding.loginBtn.setText(R.string.login_btn)
        binding.loginError.text = message
        binding.loginError.visibility = View.VISIBLE
    }

    /** Session handed to the home shell (design 01). */
    private fun openConsole() {
        startActivity(Intent(this, HomeActivity::class.java))
        finish()
    }

    private companion object {
        const val SESSION_PREFS = "chirp_session"
        const val KEY_HOST = "host"
        const val KEY_USER = "user"
        /** Emulator host loopback; 7001 = chat gateway dev port. */
        const val DEFAULT_HOST = "ws://10.0.2.2:7001"
    }
}
