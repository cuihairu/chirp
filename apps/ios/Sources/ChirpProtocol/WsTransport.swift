/// Transport seam between the connection state machine and the platform
/// socket (port of mobile_companion ws_transport.dart WsTransport via the
/// Kotlin WsTransport.kt). The connection owns the u32 framing; this layer
/// moves raw WebSocket binary messages. Tests inject a scripted fake.
public protocol WsTransport: AnyObject {
    /// Open the socket. `onResult` fires exactly once: `nil` when open, or
    /// the failure when the attempt failed — after an open failure `onClosed`
    /// never fires (the connection runs its close path itself, mirroring the
    /// dart client).
    func open(_ onResult: @escaping (Error?) -> Void)

    /// Register the binary-message handler (call before `open`). Text frames
    /// never occur on this protocol; implementations drop them.
    func onBinary(_ handler: @escaping ([UInt8]) -> Void)

    /// Register the down handler — fires exactly once when the socket is
    /// down: after `close`, after a remote close, or after an error. Never
    /// fires before `open` completes.
    func onClosed(_ handler: @escaping () -> Void)

    /// Send one binary message. False when the socket is already down.
    func send(_ data: [UInt8]) -> Bool

    /// Best-effort graceful close; the down handler still fires.
    func close()
}

// TODO(Darwin): the real transport is URLSessionWebSocketTask, Darwin-only —
// it does not exist in swift-corelibs-foundation on Linux, so this batch
// ships the seam plus scripted fakes (the Linux `swift test` gate drives the
// state machine through them). The URLSessionWebSocketTask adapter drops in
// on the Darwin side with zero connection-layer changes; see the Android
// OkHttpTransport.kt for the exact adaptation shape (frame bytes are moved
// verbatim; text frames dropped; onClosed announced exactly once).
