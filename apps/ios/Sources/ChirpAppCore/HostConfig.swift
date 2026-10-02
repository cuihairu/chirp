import Foundation

/// Normalized server endpoints for the app planes.
///
/// Defaults target the iOS simulator, where the Mac's loopback is reachable
/// directly (`127.0.0.1` — Android emulators need the 10.0.2.2 alias, iOS
/// does not). Real devices need a LAN host; the login screen keeps the host
/// editable so no build-time switch is required (the Android shell's
/// "configurable host comes with real-device debugging" note, done here).
public struct HostConfig: Equatable {
    /// Chat-plane WebSocket endpoint (dev default: chat gateway on 7001).
    public let chatUrl: URL
    /// Device-plane WebSocket endpoint (app_gateway on 5201, push
    /// registration — wired in the APNs phase, parsed here so the config
    /// surface is settled once).
    public let deviceUrl: URL

    public init(chatUrl: URL, deviceUrl: URL) {
        self.chatUrl = chatUrl
        self.deviceUrl = deviceUrl
    }

    /// Derives both endpoints from one editable host entry.
    ///
    /// Accepts `host`, `host:port` (chat port; the device port stays the
    /// fixed 5201) or a full `ws(s)://host[:port]` chat URL. A bare host or
    /// non-ws scheme defaults to `ws://` — the dev gateway speaks plain ws;
    /// production TLS arrives with the deployment batch that fronts it.
    public static func resolve(_ rawHost: String) -> HostConfig? {
        var text = rawHost.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty {
            return nil
        }
        // Full chat URL: take scheme+authority as-is, drop any path (the
        // gateway serves at the root, the web `/ws/chat` form is the vite
        // proxy's routing, not the gateway's).
        if text.contains("://") {
            guard let url = URL(string: text), let host = url.host, !host.isEmpty else {
                return nil
            }
            let scheme = (url.scheme ?? "ws").lowercased()
            let port = url.port.map { ":\($0)" } ?? ""
            text = "\(scheme)://\(host)\(port)"
        } else {
            text = "ws://\(text)"
        }
        guard let chat = URL(string: text), chat.host != nil else {
            return nil
        }
        // Device plane: same host, fixed app_gateway port.
        guard let deviceHost = chat.host else { return nil }
        let device = URL(string: "ws://\(deviceHost):5201") ?? chat
        return HostConfig(chatUrl: chat, deviceUrl: device)
    }

    public static let simulatorDefault = HostConfig(
        chatUrl: URL(string: "ws://127.0.0.1:7001")!,
        deviceUrl: URL(string: "ws://127.0.0.1:5201")!
    )
}
