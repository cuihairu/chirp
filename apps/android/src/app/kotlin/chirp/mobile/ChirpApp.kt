package chirp.mobile

import android.app.Application
import android.content.Context

/**
 * Application owner of the process-scoped [ChirpSession]. Screens reach it
 * through [chirpSession] — never cache the session across configuration
 * changes via an activity field.
 */
class ChirpApp : Application() {

    val session: ChirpSession = ChirpSession(this)

    companion object {
        /** Convenience accessor: `context.chirpSession` anywhere in the app. */
        val Context.chirpSession: ChirpSession
            get() = (applicationContext as ChirpApp).session
    }
}
