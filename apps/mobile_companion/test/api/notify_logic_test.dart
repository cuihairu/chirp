import 'package:flutter_test/flutter_test.dart';
import 'package:chirp_mobile/api/notify_logic.dart';

void main() {
  test('notifies when app is in background', () {
    expect(
      shouldNotifyFor(
        appInForeground: false,
        openChannelKey: 'p:a|b',
        messageChannelKey: 'p:a|b',
      ),
      isTrue,
    );
  });

  test('notifies for other channels while chatting elsewhere', () {
    expect(
      shouldNotifyFor(
        appInForeground: true,
        openChannelKey: 'p:a|b',
        messageChannelKey: 'g:g1',
      ),
      isTrue,
    );
  });

  test('silent for the open channel in the foreground', () {
    expect(
      shouldNotifyFor(
        appInForeground: true,
        openChannelKey: 'p:a|b',
        messageChannelKey: 'p:a|b',
      ),
      isFalse,
    );
  });

  test('notification copy', () {
    expect(privateTitle('alice'), '来自 alice 的私信');
    expect(groupTitle('公会A', 'bob'), '公会A · bob');
  });
}
