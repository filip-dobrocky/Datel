import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:network_info_plus/network_info_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:wifi_iot/wifi_iot.dart';

import 'osc.dart';

// Ports from lib/Ecosystem/src/EcosystemConfig.h.
const cmdPort = 54345;
const infoPort = 54355;
const staleAfter = Duration(seconds: 15);

/// One installation sharing the mesh. Both listen on the same gateway; the OSC
/// base address picks who obeys. Telemetry is told apart by id range.
class Profile {
  Profile(this.name, this.base, this.ids);
  final String name, base;
  final List<int> ids; // shown even before their first ping
  bool paused = false;
  double velocity = 1, density = 0;

  bool owns(int id) => base == '/birb' ? id >= 100 : id < 100;
}

class Node {
  Node(this.id);
  final int id;
  DateTime? lastPing;
  int? fw;
  double? battery;
  bool suspended = false;
  double velocity = 1;

  bool get alive =>
      lastPing != null && DateTime.now().difference(lastPing!) < staleAfter;
}

class Swarm extends ChangeNotifier {
  final profiles = [
    Profile('Datel', '/datel', List.generate(12, (i) => i)),
    Profile('Birb', '/birb', List.generate(8, (i) => 100 + i)),
  ];
  final nodes = <int, Node>{};
  String ip = '10.0.0.1';
  String status = 'starting…';
  bool busy = false;

  // Settings (persisted).
  String ssid = '', pass = '', manualIp = '';
  double defaultVelocity = 1;

  late SharedPreferences _prefs;
  RawDatagramSocket? _sock;
  final _lastSend = <String, DateTime>{};

  Node node(int id) => nodes.putIfAbsent(id, () {
        final n = Node(id);
        n.velocity = _prefs.getDouble('vel_$id') ?? defaultVelocity;
        return n;
      });

  List<Node> nodesOf(Profile p) {
    final ids = {...p.ids, ...nodes.keys.where(p.owns)}.toList()..sort();
    return ids.map(node).toList();
  }

  bool get live => nodes.values.any((n) => n.alive);

  Future<void> start() async {
    _prefs = await SharedPreferences.getInstance();
    ssid = _prefs.getString('ssid') ?? '';
    pass = _prefs.getString('pass') ?? '';
    manualIp = _prefs.getString('manualIp') ?? '';
    defaultVelocity = _prefs.getDouble('defaultVelocity') ?? 1;
    for (final p in profiles) {
      p.velocity = defaultVelocity;
    }
    await _bind();
    findNode(); // slow on Windows (powershell); UI shows status meanwhile
    // Repaint ping freshness even when nothing arrives.
    Timer.periodic(const Duration(seconds: 1), (_) => notifyListeners());
  }

  Future<void> saveSettings(
      {required String ssid,
      required String pass,
      required String manualIp,
      required double defaultVelocity}) async {
    this.ssid = ssid;
    this.pass = pass;
    this.manualIp = manualIp;
    this.defaultVelocity = defaultVelocity;
    await _prefs.setString('ssid', ssid);
    await _prefs.setString('pass', pass);
    await _prefs.setString('manualIp', manualIp);
    await _prefs.setDouble('defaultVelocity', defaultVelocity);
    await findNode();
  }

  // --- socket ---------------------------------------------------------------
  // One socket on the telemetry port: receives /info/* broadcasts and sends
  // commands. Rebound after Wi-Fi changes or any socket error, so a network
  // drop never leaves the app silently deaf.
  // Shared future: overlapping callers must not each open (and leak) a socket.
  Future<void>? _binding;
  Future<void> _bind() => _binding ??= _doBind().whenComplete(() => _binding = null);

  Future<void> _doBind() async {
    _sock?.close();
    _sock = null;
    try {
      final s = await RawDatagramSocket.bind(InternetAddress.anyIPv4, infoPort,
          reuseAddress: true);
      s.listen((e) {
        if (e != RawSocketEvent.read) return;
        final dg = s.receive();
        if (dg != null) _onPacket(dg.data);
      }, onError: (Object e) {
        _setStatus('socket error: $e');
        if (_sock == s) _sock = null;
      }, onDone: () {
        if (_sock == s) _sock = null;
      });
      _sock = s;
    } catch (e) {
      _setStatus('cannot bind :$infoPort — $e');
    }
  }

  void _onPacket(Uint8List data) {
    final msg = decodeOsc(data);
    if (msg == null) return;
    final (addr, args) = msg;
    if (args.isEmpty || args[0] is! int) return;
    final n = node(args[0] as int);
    final v = args.length > 1 ? args[1] : null;
    switch (addr) {
      case '/info/ping':
        n.lastPing = DateTime.now();
        if (v is int && v > 0) n.fw = v;
      case '/info/battery':
        if (v is num) n.battery = v.toDouble();
      case '/info/suspended':
        if (v is int) n.suspended = v != 0;
      default:
        return;
    }
    notifyListeners();
  }

  Future<void> send(Profile p, String path, [List<Object> args = const []]) async {
    if (_sock == null) await _bind();
    final s = _sock;
    if (s == null) return;
    try {
      final sent = s.send(encodeOsc('${p.base}$path', args),
          InternetAddress(ip), cmdPort);
      if (sent == 0) _setStatus('send failed (network down?)');
    } catch (e) {
      // Usually just no route (not on the mesh); the socket itself is fine.
      _setStatus('send error: $e');
    }
  }

  /// Slider-rate commands: each one is rebroadcast over the whole mesh, so
  /// cap them. Call with last=true on drag end so the last value always lands.
  void sendThrottled(Profile p, String path, List<Object> args,
      {bool last = false}) {
    final key = '${p.base}$path${args.length > 1 ? args[0] : ''}';
    final now = DateTime.now();
    final prev = _lastSend[key];
    if (!last &&
        prev != null &&
        now.difference(prev) < const Duration(milliseconds: 100)) {
      return;
    }
    _lastSend[key] = now;
    send(p, path, args);
  }

  // --- commands (mirror SW/sc/osc.scd) --------------------------------------
  void setPaused(Profile p, bool on) {
    p.paused = on;
    send(p, '/pause', [on ? 1 : 0]);
    notifyListeners();
  }

  void setVelocity(Profile p, double v, {bool last = false}) {
    p.velocity = v;
    sendThrottled(p, '/velocity', [v], last: last);
    notifyListeners();
  }

  void setNodeVelocity(Profile p, Node n, double v, {bool last = false}) {
    n.velocity = v;
    sendThrottled(p, '/velocity/id', [n.id, v], last: last);
    if (last) _prefs.setDouble('vel_${n.id}', v);
    notifyListeners();
  }

  /// Global /velocity overrides every per-node value on the nodes too.
  void resetVelocities(Profile p) {
    p.velocity = defaultVelocity;
    for (final n in nodesOf(p)) {
      n.velocity = defaultVelocity;
      _prefs.remove('vel_${n.id}');
    }
    send(p, '/velocity', [defaultVelocity]);
    notifyListeners();
  }

  void setDensity(Profile p, double d, {bool last = false}) {
    p.density = d;
    sendThrottled(p, '/density', [d], last: last);
    notifyListeners();
  }

  // Datel
  void peck(Profile p, int id, double freq, double dur, double curve, double amp) =>
      send(p, '/peck', [id, freq, dur, curve, amp]);
  void pattern(Profile p, int id, String pat) => send(p, '/pattern', [id, pat]);

  // birb
  void tweet(Profile p, int id) => send(p, '/tweet/id', [id]);
  void birbPattern(Profile p, int id, List<int> words) =>
      send(p, '/pattern/id', [id, ...words]);

  // --- network --------------------------------------------------------------
  /// The node we talk to is always the gateway of the softAP we joined
  /// (painlessMesh hands out 10.x.y.1) — same as ~findNode in osc.scd.
  /// Returns true when a node address is known.
  Future<bool> findNode() async {
    if (manualIp.isNotEmpty) {
      ip = manualIp;
      _setStatus('using manual IP $ip');
      return true;
    }
    String? gw;
    try {
      if (Platform.isWindows) {
        final r = await Process.run('powershell', [
          '-NoProfile',
          '-Command',
          "(Get-NetIPConfiguration | Where-Object { \$_.IPv4DefaultGateway.NextHop -like '10.*' } | Select-Object -First 1).IPv4DefaultGateway.NextHop",
        ]);
        gw = (r.stdout as String).trim();
      } else if (Platform.isLinux) {
        final r = await Process.run('sh', ['-c', "ip route | awk '/^default via 10\\./{print \$3; exit}'"]);
        gw = (r.stdout as String).trim();
      } else {
        gw = await NetworkInfo().getWifiGatewayIP();
      }
    } catch (e) {
      _setStatus('findNode: $e');
    }
    if (gw != null && gw.startsWith('10.')) {
      ip = gw;
      _setStatus('node at $ip');
      return true;
    } else {
      ip = '10.0.0.1';
      _setStatus('no 10.x.y.1 gateway — is Wi-Fi on a node AP? (got ${gw ?? '-'})');
      return false;
    }
  }

  Future<void> reconnectWifi() async {
    if (ssid.isEmpty) return _setStatus('set the mesh SSID in settings first');
    busy = true;
    _setStatus('joining $ssid…');
    try {
      if (Platform.isWindows) {
        await _windowsJoin();
      } else if (Platform.isAndroid) {
        await WiFiForIoTPlugin.forceWifiUsage(false);
        final ok = await WiFiForIoTPlugin.connect(ssid,
            password: pass,
            security: pass.isEmpty ? NetworkSecurity.NONE : NetworkSecurity.WPA,
            withInternet: false);
        if (!ok) throw 'connect refused';
        // The AP has no internet: pin this process's sockets to it.
        await WiFiForIoTPlugin.forceWifiUsage(true);
      } else {
        throw 'not supported on this platform — join manually';
      }
      // Wait for DHCP to hand us the 10.x.y.1 gateway.
      for (var i = 0; i < 15; i++) {
        await Future<void>.delayed(const Duration(seconds: 1));
        if (await findNode()) break;
      }
      await _bind();
    } catch (e) {
      _setStatus('Wi-Fi: $e');
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  // netsh can only connect to a saved profile, so (re)write one first.
  Future<void> _windowsJoin() async {
    if (pass.isNotEmpty) {
      String x(String s) => s
          .replaceAll('&', '&amp;')
          .replaceAll('<', '&lt;')
          .replaceAll('>', '&gt;');
      final f = File('${Directory.systemTemp.path}\\eco_wlan.xml');
      await f.writeAsString('''<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<name>${x(ssid)}</name>
<SSIDConfig><SSID><name>${x(ssid)}</name></SSID></SSIDConfig>
<connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
<MSM><security>
<authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>${x(pass)}</keyMaterial></sharedKey>
</security></MSM>
</WLANProfile>''');
      final r = await Process.run(
          'netsh', ['wlan', 'add', 'profile', 'filename=${f.path}', 'user=current']);
      await f.delete();
      if (r.exitCode != 0) throw (r.stdout as String).trim();
    }
    final r = await Process.run('netsh', ['wlan', 'connect', 'name=$ssid']);
    if (r.exitCode != 0) throw (r.stdout as String).trim();
  }

  void _setStatus(String s) {
    status = s;
    notifyListeners();
  }
}
