import ChirpAppCore
import ChirpProtocol
import ChirpProtos
// trimmingCharacters(in:)/Date() come from Foundation; the original single-file
// AppModel resolved them through SwiftUI/UIKit's re-export, this file cannot.
import Foundation

/// 好友/在线状态面(P4c)与群面板(P4e):名册拉取、申请/删除好友、在线状态
/// 切换、群成员管理与署名/标题读数。AppModel 的 extension 拆分(纯搬运);
/// 状态字段与聊天事件编排仍在 AppModel.swift。
@MainActor
extension AppModel {
    // ---- 好友/在线状态面(P4c) ---------------------------------------------

    /// 好友面板的整表刷新(打开面板时调):名册换入后按名册批量拉一次在线
    /// 状态。任一路失败置降级文案,面板照开——在线状态缺失只影响小圆点。
    func loadFriends() {
        guard let service = service else {
            socialPanelNotice = "尚未登录"
            return
        }
        socialPanelNotice = nil
        service.refreshFriends()
            .flatMap { [weak self] _ -> Promise<Chirp_Common_ErrorCode> in
                guard let self = self, let service = self.service else {
                    return Promise<Chirp_Common_ErrorCode>.completed(.ok)
                }
                return service.pullPresence(of: service.friends.friends())
            }
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    self.rebuildSocial()
                    if case .failure(let err) = outcome {
                        self.socialPanelNotice = "名册拉取失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 改自己的在线状态(广播给好友)。失败只提示,面板状态保持原值。
    func setMyPresence(_ status: Chirp_Social_PresenceStatus) {
        guard let service = service else { return }
        service.setPresence(status).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.myPresence = status
                case .success(let code):
                    self.toast = "状态切换被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "状态切换失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 发起好友申请(空 id / 自己的 id 由 service 侧守卫,这里只做去空白)。
    func addFriendTapped(targetUserId: String) {
        let target = targetUserId.trimmingCharacters(in: .whitespaces)
        guard !target.isEmpty, let service = service else { return }
        service.addFriend(targetUserId: target).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.toast = "已向 \(target) 发出好友申请"
                    self.rebuildSocial()
                case .success(let code):
                    self.toast = "申请被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "申请失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 同意/拒绝一条入向申请;名册由 FRIEND_ACCEPTED_NOTIFY 补。
    func respondToRequestTapped(requestId: String, accept: Bool) {
        guard let service = service else { return }
        service.respondToRequest(requestId: requestId, accept: accept)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let code) where code == .ok:
                        self.rebuildSocial()
                    case .success(let code):
                        self.toast = "处理被拒:\(self.codeName(code))"
                    case .failure(let err):
                        self.toast = "处理失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 删好友(服务端对称幂等,对方走 notify)。
    func removeFriendTapped(userId: String) {
        guard let service = service else { return }
        service.removeFriend(targetUserId: userId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.rebuildSocial()
                case .success(let code):
                    self.toast = "删除被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "删除失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 名册/申请/在线状态 → @Published 视图行。TTL 过期(70s 无增量)的快照
    /// 按离线渲染,漏掉的 disconnect notify 由这一层兜底。
    func rebuildSocial() {
        guard let service = service else {
            socialFriends = []
            pendingRequests = []
            pendingOutIds = []
            return
        }
        let nowMs = Int64(Date().timeIntervalSince1970 * 1000)
        socialFriends = service.friends.friends().map { userId in
            let fresh = service.presence.isFresh(userId, nowMs: nowMs)
            let entry = fresh ? service.presence.entry(userId) : nil
            return SocialFriendRow(
                userId: userId,
                status: entry?.status ?? .offline,
                statusMessage: entry?.statusMessage ?? "")
        }
        pendingRequests = service.friends.pendingIn()
        pendingOutIds = service.friends.pendingOut()
    }

    // ---- 群面板(P4e,web GroupDialogs 对齐)-----------------------------------

    /// 名单镜像 → @Published(sessions 列行标题也从这里取名)。
    func refreshGroupsRoster() {
        groupRoster = service?.groups.entries() ?? []
    }

    /// 群设置面板打开:清旧清单再拉当前群成员(web 打开 Dialog 同款)。
    func openGroupPanel(groupId: String) {
        groupMembers = []
        groupPanelNotice = nil
        loadGroupMembers(groupId: groupId)
    }

    func loadGroupMembers(groupId: String) {
        guard let service = service else { return }
        service.loadGroupMembers(groupId: groupId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let resp):
                    if resp.code == .ok {
                        self.groupMembers = resp.members
                        self.groupPanelNotice = resp.members.isEmpty ? "尚无成员" : nil
                    } else {
                        self.groupPanelNotice = "成员拉取被拒:\(self.codeName(resp.code))"
                    }
                case .failure(let err):
                    self.groupPanelNotice = "成员拉取失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 建群;成功即跳进新群的聊天面(web 创建后进频道同款)。
    func createGroupTapped(name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty, let service = service else { return }
        service.createGroup(name: trimmed).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let groupId) where !groupId.isEmpty:
                    self.toast = "群已创建"
                    self.refreshSessions()
                    self.refreshGroupsRoster()
                    self.requestGroupChat(groupId: groupId)
                case .success:
                    self.toast = "建群失败(服务端拒绝或断线)"
                case .failure(let err):
                    self.toast = "建群失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 邀人进群(INVITE 直接入群,无待确认态——web「添加成员」同款)。
    /// 成功后重拉成员清单(成员数与名单都是服务端权威)。
    func inviteToGroupTapped(groupId: String, targetUserId: String) {
        let target = targetUserId.trimmingCharacters(in: .whitespaces)
        guard !target.isEmpty, let service = service else { return }
        service.inviteToGroup(groupId: groupId, targetUserId: target)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let code) where code == .ok:
                        self.toast = "已添加 \(target)"
                        self.loadGroupMembers(groupId: groupId)
                    case .success(let code):
                        self.toast = "添加被拒:\(self.codeName(code))"
                    case .failure(let err):
                        self.toast = "添加失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 群主移出成员;成功后重拉成员清单。
    func kickGroupMemberTapped(groupId: String, targetUserId: String) {
        guard let service = service else { return }
        service.kickMember(groupId: groupId, targetUserId: targetUserId)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let code) where code == .ok:
                        self.toast = "已移出 \(targetUserId)"
                        self.loadGroupMembers(groupId: groupId)
                    case .success(let code):
                        self.toast = "移出被拒:\(self.codeName(code))"
                    case .failure(let err):
                        self.toast = "移出失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 设置群昵称(本人或 MODERATOR+ 可改):空串=清除。回原始应答,壳层按
    /// code 分流文案;快照经 2124 notify 折叠刷新(操作者自身在扇出名里,
    /// 不本地改),memberRow 与消息署名随 aliases 快照重渲。
    func setGroupAliasTapped(groupId: String, targetUserId: String, alias: String) {
        guard let service = service else { return }
        service.setMemberAlias(groupId: groupId, targetUserId: targetUserId, alias: alias)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let resp) where resp.code == .ok:
                        self.toast = alias.isEmpty ? "已清除群昵称" : "已设置群昵称"
                    case .success(let resp):
                        self.toast = "群昵称被拒:\(self.codeName(resp.code))"
                    case .failure(let err):
                        self.toast = "群昵称失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 群禁言(MODERATOR+,seconds=0 解禁):回原始应答按 code 分流;
    /// 成员行徽章与发送门都读服务端 muted_until_ts,经 2252 notify→重拉
    /// 名册刷新,此处不再重拉(与 web/harmony 同款)。
    func setGroupMuteTapped(groupId: String, targetUserId: String, seconds: Int64) {
        guard let service = service else { return }
        service.setGroupMute(groupId: groupId, targetUserId: targetUserId, seconds: seconds)
            .onComplete { [weak self] outcome in
                Task { @MainActor in
                    guard let self = self else { return }
                    switch outcome {
                    case .success(let resp) where resp.code == .ok:
                        self.toast = seconds == 0 ? "已解除禁言" : "已禁言"
                    case .success(let resp):
                        self.toast = "禁言被拒:\(self.codeName(resp.code))"
                    case .failure(let err):
                        self.toast = "禁言失败:\(err.localizedDescription)"
                    }
                }
            }
    }

    /// 退群;成功由 sessionDropped 事件弹栈关面板(web onLeft 退导航同款)。
    func leaveGroupTapped(groupId: String) {
        guard let service = service else { return }
        service.leaveGroup(groupId: groupId).onComplete { [weak self] outcome in
            Task { @MainActor in
                guard let self = self else { return }
                switch outcome {
                case .success(let code) where code == .ok:
                    self.toast = "已退出群组"
                case .success(let code):
                    self.toast = "退群被拒:\(self.codeName(code))"
                case .failure(let err):
                    self.toast = "退群失败:\(err.localizedDescription)"
                }
            }
        }
    }

    /// 群聊署名读数:活跃会话为群时读 aliases 快照(空=未设),其余回退空串
    /// 由调用方回退本名/ID(web groupAliasOf 同位)。
    func groupAlias(of userId: String) -> String {
        guard let channel = activeChannel, channel.kind == .group, let service = self.service
        else { return "" }
        return service.aliases.aliasOf(groupId: channel.channelId, userId: userId)
    }

    /// 会话行/聊天面标题:DM 显示对端 id,群显示群名(名单缺名回落群 id,
    /// web 同款)。
    func sessionTitle(_ session: SessionIndex.Summary) -> String {
        switch session.kind {
        case .dm: return session.peerId
        case .group: return groupName(session.peerId)
        }
    }

    /// 聊天面标题(键寻址;ChatView navigationTitle 用)。
    func chatTitle(forKey key: String) -> String {
        guard let channel = SessionChannel(key: key, selfId: currentUserId) else {
            return key
        }
        switch channel.kind {
        case .dm: return channel.peerId
        case .group: return groupName(channel.peerId)
        }
    }

    /// 群名(缺名回落群 id);群主判定给设置面板用。
    func groupName(_ groupId: String) -> String {
        service?.groups.name(groupId: groupId) ?? groupId
    }

    var isGroupOwner: Bool {
        guard let channel = activeChannel, channel.kind == .group,
            let service = service,
            let entry = service.groups.entry(groupId: channel.peerId)
        else { return false }
        return entry.ownerId == currentUserId
    }
}
