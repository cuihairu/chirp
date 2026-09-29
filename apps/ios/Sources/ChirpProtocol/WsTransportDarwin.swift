/// Darwin WebSocket transport (URLSessionWebSocketTask).
/// Only compiles on Apple platforms — Linux gate uses FakeWsTransport.
/// Port of OkHttpTransport.kt: frame bytes moved verbatim; text frames dropped;
/// onClosed announced exactly once after a successful open (pre-open failure
/// reports through open's onResult; the connection runs its own close path).
#if os(macOS) || os(iOS) || os(tvOS) || os(watchOS)
import Foundation

public final class DarwinWsTransport: WsTransport {
    private let url: URL
    private let session: URLSession
    private var task: URLSessionWebSocketTask?
    private var binaryHandler: (([UInt8]) -> Void)?
    private var closedHandler: (() -> Void)?
    private var openResultHandler: ((Error?) -> Void)?
    private var openAnnounced = false
    private var closedAnnounced = false

    public init(url: String) {
        self.url = URL(string: url)!
        // ephemeral session avoids disk cache / cookie storage
        let config = URLSessionConfiguration.ephemeral
        self.session = URLSession(configuration: config)
    }

    public func open(_ onResult: @escaping (Error?) -> Void) {
        openResultHandler = onResult
        let wsTask = session.webSocketTask(with: url)
        wsTask.resume()
        self.task = wsTask
        receiveLoop()
    }

    public func onBinary(_ handler: @escaping ([UInt8]) -> Void) {
        binaryHandler = handler
    }

    public func onClosed(_ handler: @escaping () -> Void) {
        closedHandler = handler
    }

    public func send(_ data: [UInt8]) -> Bool {
        guard let task = task, task.state == .running else { return false }
        let bytes = Data(data)
        task.send(.data(bytes)) { error in
            // send completion is fire-and-forget; connection layer handles timeouts
            if let error = error {
                // Surface via closed path — the task will transition to completed
                // and our receive loop will exit, announcing closed.
                self.announceClosedIfNeeded()
            }
        }
        return true
    }

    public func close() {
        task?.cancel(with: .normalClosure, reason: nil)
        // The receive loop will exit and announce closed.
        // If the task was never started or already completed, announce here.
        if task?.state != .running {
            announceClosedIfNeeded()
        }
    }

    private func receiveLoop() {
        task?.receive { [weak self] result in
            guard let self = self else { return }
            switch result {
            case .success(let message):
                switch message {
                case .data(let data):
                    self.binaryHandler?([UInt8](data))
                case .string:
                    // Text frames are not part of this protocol; drop them.
                    break
                @unknown default:
                    break
                }
                // Continue receiving
                self.receiveLoop()
            case .failure(let error):
                // Connection error or clean close
                if !self.openAnnounced {
                    self.openAnnounced = true
                    self.openResultHandler?(error)
                } else {
                    self.announceClosedIfNeeded()
                }
            }
        }
    }

    private func announceClosedIfNeeded() {
        guard !closedAnnounced else { return }
        closedAnnounced = true
        closedHandler?()
    }
}
#endif