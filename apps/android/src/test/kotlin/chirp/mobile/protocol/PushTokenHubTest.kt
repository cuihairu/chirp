package chirp.mobile.protocol

import kotlin.test.Test
import kotlin.test.assertEquals

/**
 * PushTokenHub：FCM 刷新 → 进程级 latest + 监听扇出的语义钉子。单例状态
 * 跨用例共存（同一 JVM），故每例用独立 token、监听 finally 摘除。
 */
class PushTokenHubTest {

    @Test
    fun rememberUpdatesLatestAndFansOut() {
        val seen = mutableListOf<String>()
        val listener = PushTokenHub.Listener { seen.add(it) }
        PushTokenHub.addListener(listener)
        try {
            // 单例 latest 可能已被其他用例占位（同 JVM 共存），不跨用例断言初值。
            PushTokenHub.remember("tok-round")
            assertEquals("tok-round", PushTokenHub.latest())
            assertEquals(listOf("tok-round"), seen)
        } finally {
            PushTokenHub.removeListener(listener)
        }
    }

    @Test
    fun blankTokenIsIgnored() {
        val seen = mutableListOf<String>()
        val listener = PushTokenHub.Listener { seen.add(it) }
        PushTokenHub.addListener(listener)
        try {
            val before = PushTokenHub.latest()
            PushTokenHub.remember("")
            PushTokenHub.remember("   ")
            assertEquals(before, PushTokenHub.latest(), "空 token 不动 latest")
            assertEquals(emptyList(), seen, "空 token 不扇出")
        } finally {
            PushTokenHub.removeListener(listener)
        }
    }

    @Test
    fun unchangedTokenDoesNotRefire() {
        val seen = mutableListOf<String>()
        val listener = PushTokenHub.Listener { seen.add(it) }
        PushTokenHub.addListener(listener)
        try {
            PushTokenHub.remember("tok-dedupe")
            PushTokenHub.remember("tok-dedupe")
            assertEquals(listOf("tok-dedupe"), seen, "同 token 幂等，只扇出一次")
        } finally {
            PushTokenHub.removeListener(listener)
        }
    }

    @Test
    fun removeListenerStopsFanOut() {
        val seen = mutableListOf<String>()
        val listener = PushTokenHub.Listener { seen.add(it) }
        PushTokenHub.addListener(listener)
        PushTokenHub.removeListener(listener)
        PushTokenHub.remember("tok-after-remove")
        assertEquals(emptyList(), seen)
    }

    @Test
    fun throwingListenerDoesNotBlockSiblings() {
        val seen = mutableListOf<String>()
        val bad = PushTokenHub.Listener { throw IllegalStateException("boom") }
        val good = PushTokenHub.Listener { seen.add(it) }
        PushTokenHub.addListener(bad)
        PushTokenHub.addListener(good)
        try {
            PushTokenHub.remember("tok-iso")
            assertEquals(listOf("tok-iso"), seen, "抛异常的监听者不拖累其余")
        } finally {
            PushTokenHub.removeListener(bad)
            PushTokenHub.removeListener(good)
        }
    }

    @Test
    fun listenerMayReenterRememberWithoutDeadlock() {
        var calls = 0
        val listener = PushTokenHub.Listener { token ->
            calls++
            if (calls == 1) PushTokenHub.remember(token) // 同 token 再入：幂等 no-op
        }
        PushTokenHub.addListener(listener)
        try {
            PushTokenHub.remember("tok-re") // 扇出在锁外，同步返回
            assertEquals(1, calls)
        } finally {
            PushTokenHub.removeListener(listener)
        }
    }
}
