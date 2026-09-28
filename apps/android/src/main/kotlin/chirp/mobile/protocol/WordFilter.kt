package chirp.mobile.protocol

/**
 * 敏感词预检：行为对齐服务端 chirp::chat::WordFilter 与 dart 端
 * WordFilterInterceptor（word_filter.dart）——词库格式、ASCII 大小写不敏感
 * 子串匹配、mask 后重建的替换语义。只滤发送侧：接收内容信任服务端已按其
 * 策略处理。实例构造后不可变，可并发复用。
 */
enum class WordFilterPolicy { REPLACE, REJECT }

class WordFilterOptions(
    val terms: List<String> = emptyList(),
    val policy: WordFilterPolicy = WordFilterPolicy.REPLACE,
    val replacement: String = "**",
)

class WordFilter(options: WordFilterOptions) {
    /** lower-cased, deduplicated, sorted。 */
    val terms: List<String> = parseWordLexicon(options.terms)
    private val policy: WordFilterPolicy = options.policy
    private val replacement: String = options.replacement

    val wordCount: Int get() = terms.size

    /**
     * replace：命中时返回改写后的内容；无命中返回 null（调用方原样放行）。
     * reject：命中返回 null 表示拦截，未命中返回原样内容。
     */
    fun filter(content: String): FilterResult {
        val lowered = asciiLower(content)
        val masked = maskHits(lowered) ?: return FilterResult(allowed = true, content = content)
        if (policy == WordFilterPolicy.REJECT) {
            return FilterResult(allowed = false, content = content)
        }
        return FilterResult(allowed = true, content = rebuild(content, masked))
    }

    /** null = 无命中；否则为按 code unit 的命中区间 mask。 */
    private fun maskHits(lowered: String): BooleanArray? {
        if (terms.isEmpty()) return null
        val masked = BooleanArray(lowered.length)
        var hit = false
        for (term in terms) {
            // 词永不为空：parseWordLexicon 丢掉空行。
            var pos = lowered.indexOf(term)
            while (pos >= 0) {
                hit = true
                for (i in pos until pos + term.length) masked[i] = true
                pos = lowered.indexOf(term, pos + term.length)
            }
        }
        return if (hit) masked else null
    }

    /** 连续命中塌缩成一次替换，未命中字符保留原大小写。 */
    private fun rebuild(content: String, masked: BooleanArray): String {
        val out = StringBuilder()
        var i = 0
        while (i < masked.size) {
            if (masked[i]) {
                out.append(replacement)
                while (i < masked.size && masked[i]) i++
            } else {
                out.append(content[i])
                i++
            }
        }
        return out.toString()
    }

    data class FilterResult(val allowed: Boolean, val content: String)

    companion object {
        /** 按服务端词库文件格式（每行一词，# 开头为注释，空行忽略）逐行解析。trim 集合与服务端一致：空格与 CR/LF，不含 tab 等其余空白。 */
        fun parseWordLexicon(lines: List<String>): List<String> {
            val unique = LinkedHashSet<String>()
            for (raw in lines) {
                var term = raw.trimEnd(' ', '\r', '\n').trimStart(' ')
                if (term.isEmpty() || term.startsWith("#")) continue
                unique.add(asciiLower(term))
            }
            return unique.sorted()
        }

        /** 只折叠 ASCII 大小写，与服务端 ToLower（默认 C locale）逐字节一致；UTF-8 多字节序列不受影响，按 code unit 匹配。 */
        fun asciiLower(text: String): String {
            val out = StringBuilder(text.length)
            for (c in text) {
                val code = c.code
                out.append(if (code in 0x41..0x5a) (code + 0x20).toChar() else c)
            }
            return out.toString()
        }
    }
}
