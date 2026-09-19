import 'package:flutter/foundation.dart';

/// Minimal observable store: notify-driven, zero dependencies. The shape
/// maps one-to-one onto the web companion's createStore (a ChangeNotifier
/// was the pre-declared Dart landing spot).
class Store<T> extends ChangeNotifier {
  Store(T initial) : _value = initial;

  T _value;

  T get value => _value;

  set value(T next) {
    if (identical(next, _value)) return;
    _value = next;
    notifyListeners();
  }

  /// Read-modify-write helper: keeps call sites honest about producing a new
  /// snapshot object.
  void update(T Function(T prev) fn) => value = fn(_value);
}
