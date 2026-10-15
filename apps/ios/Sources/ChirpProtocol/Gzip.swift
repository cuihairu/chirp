import CChirpGzip
import ChirpProtos
import Foundation

/// 消息压缩消费面（game_chat_features P2 历史读回形态）：登录声明
/// `supports_gzip` 的会话，历史清单装在 `GetHistoryResponse.messagesGzip`
/// 里——一个只带 messages 清单的同型 `GetHistoryResponse` 的 gzip（RFC 1952）
/// 容器。解压走 `CChirpGzip`（zlib shim；Swift 无 stdlib inflate，
/// Compression 框架仅 Darwin 有），与 C++/TS/C#/Kotlin 四端同语义。
public enum Gzip {
    /// Decompression-bomb cap; the C++ core primitive and the TS/C#/Kotlin
    /// readers use the same 16 MiB bound.
    public static let maxDecompressedBytes = 16 * 1024 * 1024

    /// First buffer guess: a typical history page's inflated payload is a few
    /// tens of KiB, so one pass is the common case.
    private static let initialCapacity = 64 * 1024

    /// Raised for empty input, a corrupt/truncated container, or the size cap.
    /// Mirrors the other runtimes' error shape (C++ `BadResponse`, TS
    /// `GzipError`, C# `GzipInflateError`, Kotlin `GzipError`): the caller sees
    /// the history readback fail rather than silently losing messages.
    public struct Error: Swift.Error, Equatable, CustomStringConvertible {
        public enum Reason: String {
            case emptyInput
            case corruptOrTruncated
            case overCap
        }

        public let reason: Reason

        public var description: String { "gzip: \(reason.rawValue)" }

        init(_ reason: Reason) {
            self.reason = reason
        }
    }

    /// Inflate a gzip container. The buffer grows towards the cap rather than
    /// starting at it, so the common small payload costs one pass and a bomb
    /// still aborts at 16 MiB.
    public static func decompress(_ container: [UInt8]) throws -> [UInt8] {
        if container.isEmpty {
            throw Error(.emptyInput)
        }
        var capacity = initialCapacity
        while true {
            var buf = [UInt8](repeating: 0, count: capacity)
            var produced = 0
            let rc = container.withUnsafeBufferPointer { src in
                buf.withUnsafeMutableBufferPointer { dst in
                    chirp_gzip_inflate(src.baseAddress, src.count, dst.baseAddress,
                                       dst.count, &produced)
                }
            }
            if rc == CHIRP_GZIP_OK {
                buf.removeLast(capacity - produced)
                return buf
            }
            if rc == CHIRP_GZIP_AGAIN {
                if capacity >= maxDecompressedBytes {
                    throw Error(.overCap)
                }
                capacity = min(capacity * 2, maxDecompressedBytes)
                continue
            }
            throw Error(rc == CHIRP_GZIP_INVALID ? .emptyInput : .corruptOrTruncated)
        }
    }

    /// Open the compressed-history envelope into one uniform shape: the
    /// inflated list replaces `messagesGzip`, the outer code/has_more stay as
    /// sent, and the plain form passes through untouched.
    static func inflateHistory(_ resp: Chirp_Chat_GetHistoryResponse) throws
        -> Chirp_Chat_GetHistoryResponse
    {
        if resp.messagesGzip.isEmpty {
            return resp
        }
        let inner = try Chirp_Chat_GetHistoryResponse(
            serializedBytes: try decompress([UInt8](resp.messagesGzip))
        )
        var out = resp
        out.messagesGzip = Data()
        out.messages = inner.messages
        return out
    }
}