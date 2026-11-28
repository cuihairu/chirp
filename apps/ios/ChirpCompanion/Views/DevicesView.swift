import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 设备面板(09 稿 token 化,P4b):在线端镜像(chat 面同源)+ 注册设备
/// 清单(设备面 GET_USER_DEVICES)+ 偏好(游戏在线状态开关,自组队面板
/// 移来与 android/09 稿同位)。清单不可用时降级文案占位,在线端照常
/// 展示——设备面挂不影响聊天。
struct DevicesView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        NavigationStack {
            List {
                onlineSection
                registeredSection
                gamePresenceSection
            }
            .listStyle(.plain)
            .scrollContentBackground(.hidden)
            .background(Theme.bg)
            .navigationTitle("设备")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(Theme.bg, for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("完成") { dismiss() }
                }
            }
            .onAppear { model.loadGamePresence() }
        }
    }

    @Environment(\.dismiss) private var dismiss

    // ---- 在线端(其他会话) ----------------------------------------------------

    private var onlineSection: some View {
        Section {
            if model.onlineDevices.isEmpty {
                Text("本账号暂无其他在线端")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            } else {
                ForEach(model.onlineDevices, id: \.platform) { device in
                    onlineRow(device)
                        .listRowBackground(Theme.surface)
                        .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 4, trailing: 0))
                        .listRowSeparator(.hidden)
                }            }
        } header: {
            sectionHeader("在线端(其他会话)")
        } footer: {
            sectionFooter("同平台重登会顶掉旧会话;离线条目保留最近一次状态。")
        }
    }

    private func onlineRow(_ entry: OnlineDeviceIndex.Entry) -> some View {
        HStack(spacing: 12) {
            platformTile(entry.platform)
            VStack(alignment: .leading, spacing: 2) {
                Text(entry.platform)
                    .font(Theme.fontBodySemibold)
                    .foregroundStyle(Theme.textPrimary)
                Text(entry.deviceId)
                    .font(Theme.fontMeta)
                    .foregroundStyle(Theme.textTertiary)
                    .lineLimit(1)
            }
            Spacer(minLength: 8)
            if entry.online {
                ChipText(text: "在线", tone: .live)
            } else {
                ChipText(text: "离线", tone: .neutral)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .contentShape(Rectangle())
    }

    // ---- 注册设备 -------------------------------------------------------------

    private var registeredSection: some View {
        Section {
            if let notice = model.devicesPanelNotice {
                VStack(alignment: .leading, spacing: 6) {
                    Text(notice)
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.textSecondary)
                    Button("重试") { model.loadRegisteredDevices() }
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.accentSoft)
                }
                .listRowBackground(Color.clear)
                .listRowSeparator(.hidden)
            } else if model.registeredDevices.isEmpty {
                Text("尚无注册设备")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            } else {
                ForEach(model.registeredDevices, id: \.deviceID) { device in
                    registeredRow(device)
                        .listRowBackground(Theme.surface)
                        .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 4, trailing: 0))
                        .listRowSeparator(.hidden)
                }
            }
        } header: {
            sectionHeader("注册设备")
        } footer: {
            sectionFooter("来自 GET_USER_DEVICES;推送未授权的设备为降级注册(无推送目标)。")
        }
    }

    private func registeredRow(_ device: Chirp_AppNotification_DeviceInfo) -> some View {
        HStack(spacing: 12) {
            platformTile(device.platform)
            VStack(alignment: .leading, spacing: 2) {
                Text(device.platform)
                    .font(Theme.fontBodySemibold)
                    .foregroundStyle(Theme.textPrimary)
                Text(device.deviceName.isEmpty ? device.deviceID : device.deviceName)
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
                if !device.appVersion.isEmpty {
                    Text("App \(device.appVersion)")
                        .font(Theme.fontMeta)
                        .foregroundStyle(Theme.textTertiary)
                }
            }
            Spacer(minLength: 8)
            if device.isActive {
                ChipText(text: "已激活", tone: .game)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .contentShape(Rectangle())
    }

    // ---- 偏好:游戏在线状态(09 稿,android 同位) -------------------------------

    private var gamePresenceSection: some View {
        Section {
            if let presence = model.gamePresenceState {
                Toggle("对外展示游戏在线状态", isOn: Binding(
                    get: { presence.enabled },
                    set: { model.setGamePresenceEnabledTapped($0) }))
                    .font(Theme.fontBody)
                    .foregroundStyle(Theme.textPrimary)
                    .tint(Theme.accent)
                    .listRowBackground(Theme.surface)
                if presence.gameIds.isEmpty {
                    Text(presence.enabled ? "暂无已绑定的游戏" : "开关关闭时不展示")
                        .font(Theme.fontSecondary)
                        .foregroundStyle(Theme.textTertiary)
                        .listRowBackground(Color.clear)
                        .listRowSeparator(.hidden)
                } else {
                    ForEach(presence.gameIds, id: \.self) { gameId in
                        Text(gameId)
                            .font(Theme.fontSecondary)
                            .foregroundStyle(Theme.textSecondary)
                            .listRowBackground(Color.clear)
                            .listRowSeparator(.hidden)
                    }
                }
            } else {
                Text("游戏状态面不可用(设备面未就绪)")
                    .font(Theme.fontSecondary)
                    .foregroundStyle(Theme.textTertiary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
        } header: {
            sectionHeader("偏好")
        } footer: {
            sectionFooter("开关只冻结对外广播;游戏绑定关系由游戏侧维护。")
        }
    }

    // ---- 共用件 ---------------------------------------------------------------

    /// 平台图标瓦片(36 圆角,surface-2 底):平台字符串映射线性图标。
    @ViewBuilder
    private func platformTile(_ platform: String) -> some View {
        let name = Self.platformIcon(platform)
        Image(systemName: name)
            .font(.system(size: 16))
            .foregroundStyle(Theme.textSecondary)
            .frame(width: 36, height: 36)
            .background(
                RoundedRectangle(cornerRadius: Theme.radiusRow, style: .continuous)
                    .fill(Theme.surface2)
            )
    }

    private static func platformIcon(_ platform: String) -> String {
        let p = platform.lowercased()
        if p.contains("ios") || p.contains("iphone") { return "iphone" }
        if p.contains("ipad") { return "ipad.landscape" }
        if p.contains("android") { return "phone" }
        if p.contains("web") { return "globe" }
        return "laptopcomputer"
    }

    private func sectionHeader(_ title: String) -> some View {
        SectionLabel(text: title)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 12, leading: 2, bottom: -2, trailing: 0))
    }

    private func sectionFooter(_ text: String) -> some View {
        Text(text)
            .font(Theme.fontCaption)
            .foregroundStyle(Theme.textTertiary)
            .listRowBackground(Color.clear)
    }
}

#Preview {
    DevicesView().environmentObject(AppModel())
}
