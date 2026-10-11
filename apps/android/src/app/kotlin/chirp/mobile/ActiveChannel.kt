package chirp.mobile

/**
 * 当前正在看的会话键（"p:a|b" / "g:gid"）：壳层前台且打开着该会话才有值
 * （onResume 算、onPause 清）。FCM 接收服务读它做前台呈现裁决
 * （[PushForegroundPolicy] 同款语义：正在看该会话才抑制横幅）。
 *
 * 静态单例跨 Activity/Service 生存期——推送服务没有 Activity 上下文可查
 * 「现在在看什么」，进程内共享一个 @Volatile 槽位是最小接线面。
 */
object ActiveChannel {
    @Volatile
    var key: String? = null
}
