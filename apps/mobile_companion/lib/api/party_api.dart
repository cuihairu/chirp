import 'dart:typed_data';

import 'package:chirp_proto/chirp_proto.dart';
import 'package:chirp_proto/proto/party.pb.dart' as party_kick;

import '../protocol/chat_connection.dart';
import '../protocol/chirp_client.dart';
import '../protocol/msg_map.dart' as specs;
import '../state/auth_store.dart';
import '../state/party_store.dart';
import '../state/store.dart';

/// Party plane api (third websocket, port 7501): cross-game team-up.
/// Degradeable like the social plane — when the socket is unavailable chat
/// keeps working and party features hide. Sync is snapshot-based: the server
/// sends the full PartyInfo to every member on every change, so the store
/// applies snapshots and never diffs events.
class PartyApi {
  PartyApi({
    required this.conn,
    required this.auth,
    required this.party,
  });

  final ChatConnection conn;
  final AuthStore auth;
  final Store<PartyState> party;
  final List<void Function()> _unsubs = [];

  Future<bool> login(String userId) async {
    if (conn.status != ConnStatus.connected) {
      await conn.connect();
    }
    final resp = await conn.request(
        specs.login, LoginRequest(token: userId, platform: 'android'));
    if (resp.code != ErrorCode.OK) {
      conn.disconnect();
      return false;
    }
    conn.resetBackoff();
    // Notify handlers up before the pull so nothing racing it is lost, then
    // restore an in-progress membership (app restart, reconnect).
    party.update((prev) => PartyState(party: null, invites: prev.invites));
    resetInvites(party);
    start();
    await fetchMyParty();
    return true;
  }

  void logout() {
    stop();
    conn.disconnect();
  }

  void stop() {
    for (final off in _unsubs) {
      off();
    }
    _unsubs.clear();
  }

  /// Re-pull our current membership (reconnect, login race recovery).
  Future<int> fetchMyParty() async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp =
        await conn.request(specs.getMyParty, GetMyPartyRequest(userId: userId));
    if (resp.code != ErrorCode.OK) return resp.code.value;
    if (resp.inParty && resp.hasParty()) {
      applySnapshot(party, _toSnapshot(resp.party));
    } else {
      clearParty(party);
    }
    return resp.code.value;
  }

  Future<int> createParty({int maxMembers = 0}) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.createParty,
        CreatePartyRequest(
          userId: userId,
          maxMembers: maxMembers,
        ));
    if (resp.code == ErrorCode.OK && resp.hasParty()) {
      applySnapshot(party, _toSnapshot(resp.party));
    }
    return resp.code.value;
  }

  Future<int> invite(String targetUserId) async {
    final userId = auth.value.userId;
    final partyId = party.value.party?.partyId;
    if (userId == null || partyId == null) return -1;
    final resp = await conn.request(
        specs.inviteToParty,
        InviteToPartyRequest(
          userId: userId,
          partyId: partyId,
          targetUserId: targetUserId,
        ));
    return resp.code.value;
  }

  Future<int> acceptInvite(String inviteId) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.acceptPartyInvite,
        AcceptInviteRequest(
          userId: userId,
          inviteId: inviteId,
        ));
    if (resp.code == ErrorCode.OK) {
      removeInvite(party, inviteId);
      if (resp.hasParty()) applySnapshot(party, _toSnapshot(resp.party));
    }
    return resp.code.value;
  }

  Future<int> declineInvite(String inviteId) async {
    final userId = auth.value.userId;
    if (userId == null) return -1;
    final resp = await conn.request(
        specs.declinePartyInvite,
        DeclineInviteRequest(
          userId: userId,
          inviteId: inviteId,
        ));
    if (resp.code == ErrorCode.OK) removeInvite(party, inviteId);
    return resp.code.value;
  }

  /// Returns true when our leaving silently disbanded the party.
  Future<bool> leaveParty() async {
    final userId = auth.value.userId;
    final partyId = party.value.party?.partyId;
    if (userId == null || partyId == null) return false;
    final resp = await conn.request(
        specs.leaveParty,
        LeavePartyRequest(
          userId: userId,
          partyId: partyId,
        ));
    if (resp.code != ErrorCode.OK) return false;
    clearParty(party);
    return resp.partyDisbanded;
  }

  /// Leader-only; the members learn via PARTY_LEFT_NOTIFY reason "kicked".
  Future<int> kickMember(String targetUserId) async {
    final userId = auth.value.userId;
    final partyId = party.value.party?.partyId;
    if (userId == null || partyId == null) return -1;
    final resp = await conn.request(
        specs.kickPartyMember,
        party_kick.KickMemberRequest(
          userId: userId,
          partyId: partyId,
          targetUserId: targetUserId,
        ));
    return resp.code.value;
  }

  /// Leader-only teardown; members learn via PARTY_DISBANDED_NOTIFY.
  Future<int> disbandParty() async {
    final userId = auth.value.userId;
    final partyId = party.value.party?.partyId;
    if (userId == null || partyId == null) return -1;
    final resp = await conn.request(
        specs.disbandParty,
        DisbandPartyRequest(
          userId: userId,
          partyId: partyId,
        ));
    return resp.code.value;
  }

  /// Leader-only succession hand-off.
  Future<int> transferLeader(String targetUserId) async {
    final userId = auth.value.userId;
    final partyId = party.value.party?.partyId;
    if (userId == null || partyId == null) return -1;
    final resp = await conn.request(
        specs.transferPartyLeader,
        TransferLeaderRequest(
          userId: userId,
          partyId: partyId,
          targetUserId: targetUserId,
        ));
    return resp.code.value;
  }

  Future<int> setReady(bool ready) async {
    final userId = auth.value.userId;
    final partyId = party.value.party?.partyId;
    if (userId == null || partyId == null) return -1;
    final resp = await conn.request(
        specs.setPartyReady,
        SetReadyRequest(
          userId: userId,
          partyId: partyId,
          ready: ready,
        ));
    return resp.code.value;
  }

  void start() {
    if (_unsubs.isNotEmpty) return;
    _unsubs.addAll([
      conn.onNotify(MsgID.INVITE_NOTIFY, onInviteNotify),
      conn.onNotify(MsgID.PARTY_STATE_CHANGED_NOTIFY, onStateChangedNotify),
      conn.onNotify(MsgID.PARTY_KICKED_NOTIFY, onKickedNotify),
      conn.onNotify(MsgID.PARTY_DISBANDED_NOTIFY, onDisbandedNotify),
      // JOINED/LEFT/INVITE_RESULT need no handling: JOINED and LEFT always
      // ride with (or are superseded by) a full STATE_CHANGED snapshot, and
      // the inviter's outcome surfaces through the UI reading the store.
    ]);
  }

  void onInviteNotify(Uint8List body) {
    InviteNotify notify;
    try {
      notify = InviteNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    addInvite(party, notify.inviteId, notify.fromUserId,
        notify.hasParty() ? notify.party.partyId : '');
  }

  void onStateChangedNotify(Uint8List body) {
    PartyStateChangedNotify notify;
    try {
      notify = PartyStateChangedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    if (notify.hasParty()) applySnapshot(party, _toSnapshot(notify.party));
  }

  void onKickedNotify(Uint8List body) {
    // The kicked user is no longer a member and gets no snapshot; the decode
    // is only a well-formedness gate.
    try {
      PartyKickedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    clearParty(party);
  }

  void onDisbandedNotify(Uint8List body) {
    try {
      PartyDisbandedNotify.fromBuffer(body);
    } catch (_) {
      return;
    }
    clearParty(party);
  }
}

PartySnapshot _toSnapshot(PartyInfo info) => PartySnapshot(
      partyId: info.partyId,
      leaderId: info.leaderId,
      maxMembers: info.maxMembers,
      members: info.members
          .map((m) => PartyMemberView(userId: m.userId, ready: m.ready))
          .toList(),
    );
