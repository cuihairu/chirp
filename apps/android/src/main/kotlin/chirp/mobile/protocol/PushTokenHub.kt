package chirp.mobile.protocol

import java.util.concurrent.CopyOnWriteArrayList

/**
 * Process-latest push-token memory: the FCM service (push leg) reports
 * token refreshes here and the shell fans them out to re-register while
 * logged in (the server's RegisterDevice is create-or-update keyed by
 * device_id, so a same-key overwrite swaps the token slot). Before the
 * first refresh (and whenever nobody is logged in) [latest] is simply
 * remembered — the next login registers the newest token anyway.
 *
 * Listener contract mirrors [PushTokenSource]: fan-out runs on the
 * reporting thread, in order, and each listener is isolated — one that
 * throws never breaks its siblings or the reporter. Listeners may call
 * [remember] re-entrantly (fan-out happens outside the state lock; a
 * same-token re-entry is an idempotent no-op).
 */
object PushTokenHub {

    /** Refresh fan-out: invoked with the newly remembered token. */
    fun interface Listener {
        fun onTokenRefreshed(token: String)
    }

    @Volatile private var latest: String? = null
    private val lock = Any()
    private val listeners = CopyOnWriteArrayList<Listener>()

    /** Latest token reported by the push leg, null before the first refresh. */
    fun latest(): String? = latest

    /**
     * Records a refreshed token and fans out — no-op on a blank token
     * (nothing to register) or an unchanged one (idempotent refresh).
     */
    fun remember(token: String) {
        if (token.isBlank()) return
        synchronized(lock) {
            if (token == latest) return
            latest = token
        }
        for (listener in listeners) {
            try {
                listener.onTokenRefreshed(token)
            } catch (_: Throwable) {
                // 隔离：单个坏消费者不得打断 FCM 回调线程与其余监听者。
            }
        }
    }

    /** Idempotent per instance; re-login should remove before adding. */
    fun addListener(listener: Listener) {
        listeners.add(listener)
    }

    fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }
}
