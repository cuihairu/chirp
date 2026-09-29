import Foundation

/// REPLACE masks hits in place; REJECT refuses the whole message.
public enum WordFilterPolicy {
    case replace
    case reject
}

public struct WordFilterOptions {
    public var terms: [String]
    public var policy: WordFilterPolicy
    public var replacement: String

    public init(
        terms: [String] = [],
        policy: WordFilterPolicy = .replace,
        replacement: String = "**"
    ) {
        self.terms = terms
        self.policy = policy
        self.replacement = replacement
    }
}

public struct WordFilterResult: Equatable {
    public let allowed: Bool
    public let content: String

    public init(allowed: Bool, content: String) {
        self.allowed = allowed
        self.content = content
    }
}

/// Shared sensitive-word filter (port of WordFilter.kt, same vectors as the
/// dart `word_filter.dart` and the server-side algorithm spec):
///   - lexicon format: one term per line, trimmed of trailing " \r\n" and
///     leading spaces; blank lines and `#` comments skipped; ASCII-only
///     lowercasing (UTF-8 safe); dedupe + sort.
///   - matching runs on UTF-16 code units (what Kotlin/dart strings index),
///     so mask spans line up across all three ports; adjacent masked runs
///     collapse into one replacement.
/// All folding is ASCII-only, so multi-byte sequences are never split mid-
/// character by a case fold.
public final class WordFilter {
    /// Parse output: lowercased, deduped, sorted (UTF-16 code-unit order,
    /// matching Kotlin `String.compareTo`).
    public let terms: [String]
    public let policy: WordFilterPolicy
    public let replacement: String
    private let termUnits: [[UInt16]]

    public var wordCount: Int { terms.count }

    public init(options: WordFilterOptions = WordFilterOptions()) {
        // The raw option list is treated as lexicon lines (Kotlin passes it
        // straight through parseWordLexicon).
        self.terms = WordFilter.parseWordLexicon(options.terms)
        self.policy = options.policy
        self.replacement = options.replacement
        self.termUnits = terms.map { WordFilter.asciiLowerUnits($0) }
    }

    public convenience init(
        terms: [String],
        policy: WordFilterPolicy = .replace,
        replacement: String = "**"
    ) {
        self.init(options: WordFilterOptions(terms: terms, policy: policy, replacement: replacement))
    }

    public func filter(_ content: String) -> WordFilterResult {
        let lowered = WordFilter.asciiLowerUnits(content)
        guard let masked = WordFilter.maskHits(in: lowered, terms: termUnits) else {
            return WordFilterResult(allowed: true, content: content)
        }
        if policy == .reject {
            return WordFilterResult(allowed: false, content: content)
        }
        return WordFilterResult(
            allowed: true,
            content: WordFilter.rebuild(content, masked: masked, replacement: replacement))
    }

    // ---- lexicon parsing ----------------------------------------------------

    public static func parseWordLexicon(_ lines: [String]) -> [String] {
        var seen = Set<[UInt16]>()
        var ordered: [[UInt16]] = []
        for line in lines {
            var units = Array(line.utf16)
            while let last = units.last, last == 0x20 || last == 0x0D || last == 0x0A {
                units.removeLast()
            }
            while let first = units.first, first == 0x20 {
                units.removeFirst()
            }
            if units.isEmpty || units[0] == 0x23 { continue } // blank or '#'
            let lowered = asciiFold(units)
            if seen.contains(lowered) { continue }
            seen.insert(lowered)
            ordered.append(lowered)
        }
        // Kotlin `sorted()` is UTF-16 code-unit lexicographic with the
        // shorter-prefix first — Array.lexicographicallyPrecedes matches.
        return ordered
            .sorted { $0.lexicographicallyPrecedes($1) }
            .map { String(decoding: $0, as: UTF16.self) }
    }

    /// ASCII-only case fold on UTF-16 code units ('A'..'Z' → 'a'..'z').
    public static func asciiLowerUnits(_ text: String) -> [UInt16] {
        asciiFold(Array(text.utf16))
    }

    public static func asciiLower(_ text: String) -> String {
        String(decoding: asciiLowerUnits(text), as: UTF16.self)
    }

    private static func asciiFold(_ units: [UInt16]) -> [UInt16] {
        units.map { u in (u >= 0x41 && u <= 0x5A) ? u + 0x20 : u }
    }

    // ---- matching -----------------------------------------------------------

    /// Boolean mask per code unit; nil means no hit anywhere. Overlapping and
    /// adjacent occurrences merge through the shared mask.
    private static func maskHits(in lowered: [UInt16], terms: [[UInt16]]) -> [Bool]? {
        var mask: [Bool]?
        for term in terms where !term.isEmpty && term.count <= lowered.count {
            var pos = 0
            while pos + term.count <= lowered.count {
                if Array(lowered[pos..<(pos + term.count)]) == term {
                    if mask == nil { mask = Array(repeating: false, count: lowered.count) }
                    for i in pos..<(pos + term.count) { mask![i] = true }
                    pos += term.count
                } else {
                    pos += 1
                }
            }
        }
        return mask
    }

    /// Masked runs become one replacement; everything else keeps its original
    /// casing (the mask indexes the original string's code units).
    private static func rebuild(_ content: String, masked: [Bool], replacement: String) -> String {
        let units = Array(content.utf16)
        let replacementUnits = Array(replacement.utf16)
        var out: [UInt16] = []
        var i = 0
        while i < units.count {
            if masked[i] {
                out.append(contentsOf: replacementUnits)
                while i < units.count && masked[i] { i += 1 }
            } else {
                out.append(units[i])
                i += 1
            }
        }
        return String(decoding: out, as: UTF16.self)
    }
}

public enum WordFilterLoader {
    /// Parses the server lexicon wire format (lines with CRLF/LF/CR
    /// terminators — Kotlin readLine semantics) into a filter.
    public static func load(_ text: String, policy: WordFilterPolicy = .replace) -> WordFilter {
        var lines: [String] = []
        var units: [UInt16] = []
        let all = Array(text.utf16)
        var i = 0
        while i < all.count {
            let u = all[i]
            if u == 0x0D {
                lines.append(String(decoding: units, as: UTF16.self))
                units = []
                if i + 1 < all.count && all[i + 1] == 0x0A { i += 1 } // CRLF is one terminator
            } else if u == 0x0A {
                lines.append(String(decoding: units, as: UTF16.self))
                units = []
            } else {
                units.append(u)
            }
            i += 1
        }
        // A final unterminated line still counts (readLine would yield it).
        lines.append(String(decoding: units, as: UTF16.self))
        return WordFilter(options: WordFilterOptions(
            terms: WordFilter.parseWordLexicon(lines), policy: policy))
    }
}
