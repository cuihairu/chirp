/// Pure decision for when an incoming chat message should raise a local
/// system notification — mobile twin of the web companion's
/// desktop_notify.shouldNotifyFor (tabHidden there, app-in-background here).
bool shouldNotifyFor({
  required bool appInForeground,
  required String? openChannelKey,
  required String messageChannelKey,
}) =>
    !appInForeground || messageChannelKey != openChannelKey;

/// Notification copy. Group messages lead with the group name, like the web.
String privateTitle(String fromUserId) => '来自 $fromUserId 的私信';

String groupTitle(String groupTitleText, String fromUserId) =>
    '$groupTitleText · $fromUserId';
