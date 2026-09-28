package chirp.mobile.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/** 与 mobile_companion test/protocol/word_filter_test.dart 同组测试向量对拍。 */
class WordFilterTest {
    @Test
    fun parseWordLexiconTrimsDropsBlanksAndCommentsLowerCasesAndDedupes() {
        assertEquals(
            listOf("dummy", "spam", "论坛"),
            WordFilter.parseWordLexicon(
                listOf(
                    "  Spam  ",
                    "# 注释行",
                    "",
                    "spam",
                    "  dummy\r\n",
                    "论坛",
                ),
            ),
        )
    }

    private fun run(filter: WordFilter, content: String): Pair<Boolean, String> {
        val result = filter.filter(content)
        return result.allowed to result.content
    }

    @Test
    fun replaceMasksHitsAndKeepsSurroundingCasing() {
        val filter = WordFilter(WordFilterOptions(terms = listOf("bad dog")))
        assertEquals(true to "** and **", run(filter, "Bad DOG and bad dog"))
        assertEquals(1, filter.wordCount)
    }

    @Test
    fun replaceCollapsesAdjacentTermRunsIntoOneReplacement() {
        val filter = WordFilter(WordFilterOptions(terms = listOf("ab", "bc"), replacement = "#"))
        assertEquals(true to "#", run(filter, "abc"))
    }

    @Test
    fun replaceKeepsUtf8SequencesAroundAsciiHits() {
        val filter = WordFilter(WordFilterOptions(terms = listOf("脏话", "damn")))
        assertEquals(true to "你好 ** 世界,真是**啊", run(filter, "你好 damn 世界,真是脏话啊"))
    }

    @Test
    fun rejectBlocksHitsAndPassesCleanTextUntouched() {
        val filter = WordFilter(WordFilterOptions(terms = listOf("banned"), policy = WordFilterPolicy.REJECT))
        assertEquals(false to "totally BANNED words", run(filter, "totally BANNED words"))
        assertEquals(true to "perfectly fine", run(filter, "perfectly fine"))
    }

    @Test
    fun emptyLexiconIsANoop() {
        val filter = WordFilter(WordFilterOptions())
        assertEquals(true to "anything at all", run(filter, "anything at all"))
    }

    @Test
    fun asciiLowerFoldsOnlyAscii() {
        assertEquals("abc-xyz ÄÖÜ 中文", WordFilter.asciiLower("ABC-XYZ ÄÖÜ 中文"))
        assertTrue(WordFilter.asciiLower("DOG").contains("dog"))
        assertFalse(WordFilter.asciiLower("论坛").contains("a"))
    }
}
