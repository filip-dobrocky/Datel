import 'dart:convert';
import 'dart:typed_data';

/// Minimal OSC 1.0 codec: int32 ('i'), float32 ('f'), string ('s').
/// Dart `int` -> i, `double` -> f, `String` -> s.
Uint8List encodeOsc(String address, [List<Object> args = const []]) {
  final b = BytesBuilder();
  void str(String s) {
    final bytes = utf8.encode(s);
    b.add(bytes);
    b.add(List.filled(4 - bytes.length % 4, 0)); // NUL + pad to 4
  }

  str(address);
  str(',${args.map((a) => switch (a) {
        int() => 'i',
        double() => 'f',
        String() => 's',
        _ => throw ArgumentError('unsupported OSC arg $a'),
      }).join()}');
  for (final a in args) {
    final d = ByteData(4);
    switch (a) {
      case int():
        d.setInt32(0, a);
        b.add(d.buffer.asUint8List());
      case double():
        d.setFloat32(0, a);
        b.add(d.buffer.asUint8List());
      case String():
        str(a);
    }
  }
  return b.toBytes();
}

/// Returns (address, args) or null for anything malformed / unsupported
/// (bundles included — the firmware never sends them).
(String, List<Object>)? decodeOsc(Uint8List data) {
  final d = ByteData.sublistView(data);
  var pos = 0;
  String? str() {
    final end = data.indexOf(0, pos);
    if (end < 0) return null;
    final s = utf8.decode(data.sublist(pos, end), allowMalformed: true);
    pos = (end + 4) & ~3;
    return s;
  }

  try {
    final addr = str();
    if (addr == null || !addr.startsWith('/')) return null;
    final tags = str();
    if (tags == null || !tags.startsWith(',')) return null;
    final args = <Object>[];
    for (final t in tags.substring(1).split('')) {
      switch (t) {
        case 'i':
          args.add(d.getInt32(pos));
          pos += 4;
        case 'f':
          args.add(d.getFloat32(pos));
          pos += 4;
        case 's':
          final s = str();
          if (s == null) return null;
          args.add(s);
        default:
          return null;
      }
    }
    return (addr, args);
  } on RangeError {
    return null;
  }
}
