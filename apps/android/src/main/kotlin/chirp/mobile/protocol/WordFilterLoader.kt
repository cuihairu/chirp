package chirp.mobile.protocol

import java.io.BufferedReader
import java.io.Reader

/**
 * Loads a [WordFilter] from a lexicon file in the **server format** (one
 * term per line, `#` comments, blank lines ignored — see
 * docs/design-notes/word_filter.md). This is the M4 stand-in for lexicon
 * delivery: the protocol has no lexicon-download message today, so the shell
 * reads a local file (pushed by MDM/config or bundled by the build) in the
 * exact format the server's `--word_filter_file` uses, and any future
 * download channel can feed the same loader.
 */
object WordFilterLoader {
    /** Reads the whole [reader] (and closes it) as one lexicon. */
    fun load(reader: Reader, policy: WordFilterPolicy = WordFilterPolicy.REPLACE): WordFilter {
        val lines = BufferedReader(reader).use { r ->
            generateSequence { r.readLine() }.toList()
        }
        return WordFilter(WordFilterOptions(terms = WordFilter.parseWordLexicon(lines), policy = policy))
    }
}
