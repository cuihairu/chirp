import SwiftUI

/// Approved design tokens (docs/ux/design-spec.md §2) as SwiftUI assets.
/// Dark is the canonical state (§6: light stays a proposal, not built).
/// Same values the harmony Tokens.ts and the Android theme carry; screens
/// must reach for these instead of system semantic colors so the three
/// mobile clients render one world.
enum Theme {
    // MARK: palette

    static let bg = Color(red: 0x0f / 255, green: 0x11 / 255, blue: 0x15 / 255)
    static let surface = Color(red: 0x16 / 255, green: 0x19 / 255, blue: 0x23 / 255)
    static let surface2 = Color(red: 0x1c / 255, green: 0x20 / 255, blue: 0x30 / 255)
    static let surface3 = Color(red: 0x23 / 255, green: 0x28 / 255, blue: 0x40 / 255)
    static let border = Color(red: 0x2a / 255, green: 0x30 / 255, blue: 0x46 / 255)
    static let borderSoft = Color(red: 0x22 / 255, green: 0x27 / 255, blue: 0x41 / 255)

    static let textPrimary = Color(red: 0xf3 / 255, green: 0xf4 / 255, blue: 0xf6 / 255)
    static let textSecondary = Color(red: 0x9c / 255, green: 0xa3 / 255, blue: 0xaf / 255)
    static let textTertiary = Color(red: 0x6b / 255, green: 0x72 / 255, blue: 0x80 / 255)

    static let accent = Color(red: 0x63 / 255, green: 0x66 / 255, blue: 0xf1 / 255)
    static let accentStrong = Color(red: 0x4f / 255, green: 0x46 / 255, blue: 0xe5 / 255)
    static let accentSoft = Color(red: 0xa5 / 255, green: 0xb4 / 255, blue: 0xfc / 255)
    /// 14% alpha accent wash (chip/filter selected backgrounds).
    static let accentDim = Color(red: 0x63 / 255, green: 0x66 / 255, blue: 0xf1 / 255).opacity(0.14)

    static let online = Color(red: 0x34 / 255, green: 0xd3 / 255, blue: 0x99 / 255)
    static let danger = Color(red: 0xf8 / 255, green: 0x71 / 255, blue: 0x71 / 255)
    static let warn = Color(red: 0xfb / 255, green: 0xbf / 255, blue: 0x24 / 255)

    // MARK: radii (§2.3 ladder)

    static let radiusCard: CGFloat = 16
    static let radiusRow: CGFloat = 12

    // MARK: avatars

    static let avatarSm: CGFloat = 32
    static let avatarMd: CGFloat = 44
    static let avatarLg: CGFloat = 72

    // MARK: typography (§2.2; system font only, no bundled faces)

    static let fontDisplay = Font.system(size: 21, weight: .bold)
    static let fontTitle = Font.system(size: 17, weight: .bold)
    static let fontBody = Font.system(size: 15)
    static let fontBodySemibold = Font.system(size: 15, weight: .semibold)
    static let fontSecondary = Font.system(size: 13)
    static let fontCaption = Font.system(size: 12)
    static let fontLabel = Font.system(size: 12, weight: .bold)
    static let fontMeta = Font.system(size: 11)
    static let fontChip = Font.system(size: 11, weight: .semibold)
}

/// Hue-hash letter avatar (chirp.css .avatar): circle filled with
/// hsl(h, 42%, 46%) from a stable hash of the label, first character
/// centered in white. Same label → same color within this client; the
/// conversion is exact (c = (1-|2l-1|)·s, v = l + c/2), matching the
/// Android AvatarView byte for byte.
struct AvatarView: View {
    let label: String
    var size: CGFloat = Theme.avatarMd
    var ring = false

    private var hue: Double {
        var h: Int32 = 0
        for byte in label.utf8 {
            h = 31 &* h &+ Int32(bitPattern: UInt32(byte &+ 128))
        }
        return Double(((h % 360) + 360) % 360)
    }

    private var fill: Color {
        let l = 0.46, s = 0.42
        let c = (1 - abs(2 * l - 1)) * s
        let v = l + c / 2
        return Color(hue: hue / 360, saturation: c / v, brightness: v)
    }

    var body: some View {
        Circle()
            .fill(fill)
            .overlay(
                Text(String(label.prefix(1)).uppercased())
                    .font(.system(size: size * 16 / 44, weight: .bold))
                    .foregroundStyle(.white)
            )
            .frame(width: size, height: size)
            .overlay(
                // .avatar-ring: bg gap + accent halo (profile card).
                ring ? Circle().strokeBorder(Theme.accent, lineWidth: 2).padding(2) : nil
            )
    }
}

/// Presence dot (chirp.css .presence): 12pt circle, bg border, green when
/// online, tertiary otherwise.
struct PresenceDot: View {
    var isOnline: Bool
    var body: some View {
        Circle()
            .fill(isOnline ? Theme.online : Theme.textTertiary)
            .frame(width: 12, height: 12)
            .overlay(Circle().strokeBorder(Theme.bg, lineWidth: 2.5))
    }
}

/// Status chip (chirp.css .chip): pill, 11/600, surface-2/secondary by
/// default with semantic variants.
struct ChipText: View {
    let text: String
    var tone: Tone = .neutral

    enum Tone {
        case neutral, live, game, warn, danger
    }

    var body: some View {
        Text(text)
            .font(Theme.fontChip)
            .foregroundStyle(foreground)
            .padding(.horizontal, 9)
            .padding(.vertical, 2)
            .background(background, in: Capsule())
    }

    private var foreground: Color {
        switch tone {
        case .neutral: Theme.textSecondary
        case .live: Theme.online
        case .game: Theme.accentSoft
        case .warn: Theme.warn
        case .danger: Theme.danger
        }
    }

    private var background: Color {
        switch tone {
        case .neutral: Theme.surface2
        case .live: Theme.online.opacity(0.12)
        case .game: Theme.accentDim
        case .warn: Theme.warn.opacity(0.12)
        case .danger: Theme.danger.opacity(0.12)
        }
    }
}

/// Section label (chirp.css .section-label): 12/700, tertiary, tracked.
struct SectionLabel: View {
    let text: String
    var body: some View {
        Text(text)
            .font(Theme.fontLabel)
            .tracking(0.6)
            .foregroundStyle(Theme.textTertiary)
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// Card container (chirp.css .card): surface fill, soft border, 16 radius.
struct CardBackground: ViewModifier {
    var fill: Color = Theme.surface

    func body(content: Content) -> some View {
        content
            .background(
                RoundedRectangle(cornerRadius: Theme.radiusCard, style: .continuous)
                    .fill(fill)
                    .overlay(
                        RoundedRectangle(cornerRadius: Theme.radiusCard, style: .continuous)
                            .strokeBorder(Theme.borderSoft, lineWidth: 1)
                    )
            )
    }
}

extension View {
    func chirpCard(fill: Color = Theme.surface) -> some View {
        modifier(CardBackground(fill: fill))
    }
}

/// Primary / ghost / danger buttons on the §2 component ladder.
struct ChirpButtonStyle: ButtonStyle {
    enum Kind { case primary, ghost, danger }

    var kind: Kind = .primary

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(Theme.fontBodySemibold)
            .foregroundStyle(foreground)
            .frame(height: 40)
            .padding(.horizontal, 16)
            .background(background, in: RoundedRectangle(cornerRadius: Theme.radiusRow, style: .continuous))
            .overlay(border)
            .opacity(configuration.isPressed ? 0.7 : 1)
    }

    private var foreground: Color {
        switch kind {
        case .primary: .white
        case .ghost: Theme.textSecondary
        case .danger: Theme.danger
        }
    }

    private var background: Color {
        switch kind {
        case .primary:
            // accent → accent-strong vertical, per the approved prototype.
            LinearGradient(colors: [Theme.accent, Theme.accentStrong], startPoint: .top, endPoint: .bottom)
        case .ghost: Theme.surface
        case .danger: Theme.danger.opacity(0.12)
        }
    }

    @ViewBuilder private var border: some View {
        if kind != .primary {
            RoundedRectangle(cornerRadius: Theme.radiusRow, style: .continuous)
                .strokeBorder(kind == .ghost ? Theme.border : Theme.danger.opacity(0.4), lineWidth: 1)
        }
    }
}

/// Bordered input field (chirp.css .input): surface-2 fill, border, 12
/// radius — the shared text field face across screens.
struct ChirpFieldBackground: ViewModifier {
    func body(content: Content) -> some View {
        content
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .background(
                RoundedRectangle(cornerRadius: Theme.radiusRow, style: .continuous)
                    .fill(Theme.surface2)
                    .overlay(
                        RoundedRectangle(cornerRadius: Theme.radiusRow, style: .continuous)
                            .strokeBorder(Theme.border, lineWidth: 1)
                    )
            )
    }
}

extension View {
    func chirpField() -> some View {
        modifier(ChirpFieldBackground())
    }
}
