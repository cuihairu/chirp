import ChirpAppCore
import ChirpProtos
import SwiftUI

/// 设备面板(P4b):上半=多端在线(chat 面镜像,platform 一槽),下半=
/// 注册设备清单(设备面 GET_USER_DEVICES)。清单不可用时降级文案占位,
/// 在线端照常展示——设备面挂不影响聊天。
struct DevicesView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        NavigationStack {
            List {
                Section {
                    if model.onlineDevices.isEmpty {
                        Text("本账号暂无其他在线端")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(model.onlineDevices, id: \.platform) { device in
                            HStack {
                                Image(systemName: device.online
                                    ? "circle.fill" : "circle")
                                    .foregroundStyle(device.online ? .green : .secondary)
                                    .font(.caption)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(device.platform).font(.headline)
                                    Text(device.deviceId)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                Text(device.online ? "在线" : "离线")
                                    .font(.caption)
                                    .foregroundStyle(device.online ? .green : .secondary)
                            }
                        }
                    }
                } header: {
                    Text("在线端(其他会话)")
                } footer: {
                    Text("同平台重登会顶掉旧会话;离线条目保留最近一次状态。")
                }

                Section {
                    if let notice = model.devicesPanelNotice {
                        VStack(alignment: .leading, spacing: 6) {
                            Text(notice).font(.subheadline)
                            Button("重试") { model.loadRegisteredDevices() }
                                .font(.subheadline)
                        }
                    } else if model.registeredDevices.isEmpty {
                        Text("尚无注册设备")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(model.registeredDevices, id: \.deviceID) { device in
                            registeredRow(device)
                        }
                    }
                } header: {
                    Text("注册设备")
                } footer: {
                    Text("来自 GET_USER_DEVICES;推送未授权的设备为降级注册(无推送目标)。")
                }
            }
            .navigationTitle("设备")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("完成") { dismiss() }
                }
            }
        }
    }

    @Environment(\.dismiss) private var dismiss

    private func registeredRow(_ device: Chirp_AppNotification_DeviceInfo) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(device.platform).font(.headline)
                Text(device.deviceName.isEmpty ? device.deviceID : device.deviceName)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                if !device.appVersion.isEmpty {
                    Text("App \(device.appVersion)")
                        .font(.caption2)
                        .foregroundStyle(.tertiary)
                }
            }
            Spacer()
            if device.isActive {
                Text("已激活")
                    .font(.caption)
                    .foregroundStyle(.blue)
            }
        }
    }
}

#Preview {
    DevicesView().environmentObject(AppModel())
}
