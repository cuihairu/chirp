import 'store.dart';

class PartyMemberView {
  const PartyMemberView({required this.userId, required this.ready});

  final String userId;
  final bool ready;
}

/// Narrowed copy of chirp.party.PartyInfo the UI renders.
class PartySnapshot {
  const PartySnapshot({
    required this.partyId,
    required this.leaderId,
    required this.maxMembers,
    required this.members,
  });

  final String partyId;
  final String leaderId;
  final int maxMembers;
  final List<PartyMemberView> members;
}

class PartyInvite {
  const PartyInvite({
    required this.inviteId,
    required this.fromUserId,
    required this.partyId,
  });

  final String inviteId;
  final String fromUserId;
  final String partyId;
}

/// Client-side mirror of the party plane. The backend is authoritative and
/// speaks in full snapshots: PARTY_STATE_CHANGED carries the whole PartyInfo
/// to every member (including the actor), so the mirror only ever applies
/// snapshots and clears itself on kick/disband. Invites mirror the incoming
/// queue; the service keeps no outgoing-invite query, so invites we sent are
/// not tracked (the inviter learns the outcome via INVITE_RESULT_NOTIFY).
class PartyState {
  const PartyState({required this.party, required this.invites});

  /// Current membership snapshot; null when not in a party.
  final PartySnapshot? party;

  /// Invites we received and have not answered.
  final List<PartyInvite> invites;
}

Store<PartyState> createPartyStore() => Store<PartyState>(const PartyState(
      party: null,
      invites: [],
    ));

/// Applies a full server snapshot (PARTY_STATE_CHANGED / join response).
void applySnapshot(Store<PartyState> store, PartySnapshot snapshot) {
  store.update((prev) => PartyState(party: snapshot, invites: prev.invites));
}

/// Leaving / being kicked / disbanding all land here.
void clearParty(Store<PartyState> store) {
  store.update((prev) => prev.party == null
      ? prev
      : PartyState(party: null, invites: prev.invites));
}

void addInvite(Store<PartyState> store, String inviteId, String fromUserId,
    String partyId) {
  store.update((prev) => prev.invites.any((i) => i.inviteId == inviteId)
      ? prev
      : PartyState(
          party: prev.party,
          invites: [
            ...prev.invites,
            PartyInvite(
                inviteId: inviteId, fromUserId: fromUserId, partyId: partyId),
          ],
        ));
}

void removeInvite(Store<PartyState> store, String inviteId) {
  store.update((prev) => PartyState(
        party: prev.party,
        invites: prev.invites.where((i) => i.inviteId != inviteId).toList(),
      ));
}

/// Login/relogin must not leak the previous account's invites.
void resetInvites(Store<PartyState> store) {
  store.update((prev) =>
      prev.invites.isEmpty ? prev : PartyState(party: prev.party, invites: []));
}

/// True when userId is the leader of the snapshot we hold.
bool isLeaderOf(PartyState state, String userId) =>
    state.party?.leaderId == userId;

/// Our own membership row, if we hold a snapshot.
PartyMemberView? selfMemberOf(PartyState state, String userId) {
  final members = state.party?.members ?? const <PartyMemberView>[];
  for (final member in members) {
    if (member.userId == userId) return member;
  }
  return null;
}
