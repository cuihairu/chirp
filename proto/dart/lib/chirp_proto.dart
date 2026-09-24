/// Barrel for the generated protocol messages. Apps import this one file:
///
/// ```dart
/// import 'package:chirp_proto/chirp_proto.dart';
/// ```
///
/// The .proto sources reuse a few message names across packages, so this
/// barrel hides the loser of each collision; code that needs the hidden
/// variants imports the generated file directly with a prefix (Dart has no
/// prefixed re-exports):
///
/// - KickMemberRequest/Response: chat's are exported; party's live at
///   `proto/party.pb.dart` (`import ... as party;` → party.KickMemberRequest)
/// - SenderKind: chat's are exported; the game_server_gateway variant lives
///   at `proto/game_server_gateway.pb.dart` (same direct-import escape hatch)
///
/// The individual files under `proto/` are protoc plugin output — regenerate
/// with gen_proto.sh instead of editing.
library;

export 'proto/auth.pb.dart';
export 'proto/chat.pb.dart';
export 'proto/common.pb.dart';
export 'proto/gateway.pb.dart';
export 'proto/notification.pb.dart';
export 'proto/party.pb.dart' hide KickMemberRequest, KickMemberResponse;
export 'proto/game_server_gateway.pb.dart' hide SenderKind;
export 'proto/social.pb.dart';
export 'proto/voice.pb.dart';
