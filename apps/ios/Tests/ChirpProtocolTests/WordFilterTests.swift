import XCTest
@testable import ChirpProtocol

/// Same vector group as Kotlin WordFilterTest, which mirrors the dart
/// word_filter_test group — three-platform conformance.
final class WordFilterTests: XCTestCase {
    func testParseWordLexiconTrimsDropsBlanksAndCommentsLowerCasesAndDedupes() {
        XCTAssertEqual(
            ["dummy", "spam", "论坛"],
            WordFilter.parseWordLexicon([
                "  Spam  ",
                "# 注释行",
                "",
                "spam",
                "  dummy\r\n",
                "论坛",
            ]))
    }

    private func run(_ filter: WordFilter, _ content: String) -> (Bool, String) {
        let result = filter.filter(content)
        return (result.allowed, result.content)
    }

    func testReplaceMasksHitsAndKeepsSurroundingCasing() {
        let filter = WordFilter(terms: ["bad dog"])
        let (allowed, content) = run(filter, "Bad DOG and bad dog")
        XCTAssertTrue(allowed)
        XCTAssertEqual("** and **", content)
        XCTAssertEqual(1, filter.wordCount)
    }

    func testReplaceCollapsesAdjacentTermRunsIntoOneReplacement() {
        let filter = WordFilter(terms: ["ab", "bc"], replacement: "#")
        let (allowed, content) = run(filter, "abc")
        XCTAssertTrue(allowed)
        XCTAssertEqual("#", content)
    }

    func testReplaceKeepsUtf8SequencesAroundAsciiHits() {
        let filter = WordFilter(terms: ["脏话", "damn"])
        let (allowed, content) = run(filter, "你好 damn 世界,真是脏话啊")
        XCTAssertTrue(allowed)
        XCTAssertEqual("你好 ** 世界,真是**啊", content)
    }

    func testRejectBlocksHitsAndPassesCleanTextUntouched() {
        let filter = WordFilter(terms: ["banned"], policy: .reject)
        let (blocked, blockedContent) = run(filter, "totally BANNED words")
        XCTAssertFalse(blocked)
        XCTAssertEqual("totally BANNED words", blockedContent)
        let (allowed, clean) = run(filter, "perfectly fine")
        XCTAssertTrue(allowed)
        XCTAssertEqual("perfectly fine", clean)
    }

    func testEmptyLexiconIsANoop() {
        let filter = WordFilter()
        let (allowed, content) = run(filter, "anything at all")
        XCTAssertTrue(allowed)
        XCTAssertEqual("anything at all", content)
    }

    func testAsciiLowerFoldsOnlyAscii() {
        XCTAssertEqual("abc-xyz ÄÖÜ 中文", WordFilter.asciiLower("ABC-XYZ ÄÖÜ 中文"))
        XCTAssertTrue(WordFilter.asciiLower("DOG").contains("dog"))
        XCTAssertFalse(WordFilter.asciiLower("论坛").contains("a"))
    }
}
