import 'dart:typed_data';

import 'package:eco_controller/osc.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('encodes byte-exact', () {
    // "/a\0\0" ",if\0" 0x00000007 1.0f
    expect(encodeOsc('/a', [7, 1.0]), [
      0x2f, 0x61, 0, 0, 0x2c, 0x69, 0x66, 0, //
      0, 0, 0, 7, 0x3f, 0x80, 0, 0,
    ]);
    // address of exactly 4 bytes still gets a full NUL word
    expect(encodeOsc('/abc').length, 8 + 4);
  });

  test('round-trips', () {
    final pkt = encodeOsc('/datel/peck', [3, 14.0, 2000.0, 0.0, 1.0]);
    final (addr, args) = decodeOsc(pkt)!;
    expect(addr, '/datel/peck');
    expect(args, [3, 14.0, 2000.0, 0.0, 1.0]);

    final (a2, args2) = decodeOsc(encodeOsc('/datel/pattern', [1, 'xx_p750AFF_xx_']))!;
    expect(a2, '/datel/pattern');
    expect(args2, [1, 'xx_p750AFF_xx_']);
  });

  test('rejects garbage', () {
    expect(decodeOsc(Uint8List.fromList([1, 2, 3])), isNull);
    expect(decodeOsc(Uint8List.fromList(encodeOsc('/x', [1]).sublist(0, 9))), isNull);
  });
}
