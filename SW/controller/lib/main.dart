import 'package:flutter/material.dart';

import 'swarm.dart';

final swarm = Swarm();

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await swarm.start();
  runApp(const App());
}

const _green = Color(0xFF2ECC71);
const _pink = Color(0xFFE88AA5);

// LiPo range for the node-tile battery fill. _batEmpty matches the firmware's
// low-battery LED threshold (SW/Datel/src/main.cpp measure_battery).
// ponytail: linear in voltage; LiPo discharge isn't, use a lookup curve if the bar misleads.
const _batEmpty = 3.3, _batFull = 4.2;
double _batteryLevel(double v) => ((v - _batEmpty) / (_batFull - _batEmpty)).clamp(0.0, 1.0);

class App extends StatelessWidget {
  const App({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Ecosystem',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          brightness: Brightness.dark,
          colorSchemeSeed: const Color(0xFF3D5AFE),
          scaffoldBackgroundColor: const Color(0xFF111111),
        ),
        home: ListenableBuilder(listenable: swarm, builder: (_, _) => Home()), // no const: must rebuild on swarm changes
      );
}

class Home extends StatelessWidget {
  const Home({super.key});

  @override
  Widget build(BuildContext context) => DefaultTabController(
        length: swarm.profiles.length,
        child: Scaffold(
          appBar: AppBar(
            toolbarHeight: 0,
            bottom: TabBar(tabs: [for (final p in swarm.profiles) Tab(text: p.name)]),
          ),
          body: Column(children: [
            ConnectionBar(), // no const: must rebuild on swarm changes
            Expanded(
              child: TabBarView(children: [
                for (final p in swarm.profiles) ProfilePage(p),
              ]),
            ),
          ]),
        ),
      );
}

class ConnectionBar extends StatelessWidget {
  const ConnectionBar({super.key});

  @override
  Widget build(BuildContext context) => Material(
        color: const Color(0xFF1C1C1C),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(12, 6, 4, 6),
          child: Row(children: [
            Icon(Icons.circle, size: 12, color: swarm.live ? _green : Colors.grey),
            const SizedBox(width: 8),
            Expanded(
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Text(swarm.ip, style: const TextStyle(fontWeight: FontWeight.bold)),
                Text(swarm.status,
                    style: Theme.of(context).textTheme.bodySmall,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis),
              ]),
            ),
            IconButton(
                tooltip: 'Find node',
                icon: const Icon(Icons.travel_explore),
                onPressed: swarm.findNode),
            IconButton(
                tooltip: 'Reconnect Wi-Fi',
                icon: swarm.busy
                    ? const SizedBox.square(
                        dimension: 20, child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.wifi_find),
                onPressed: swarm.busy ? null : swarm.reconnectWifi),
            IconButton(
                tooltip: 'Settings',
                icon: const Icon(Icons.settings),
                onPressed: () => showDialog<void>(
                    context: context, builder: (_) => const SettingsDialog())),
          ]),
        ),
      );
}

class ProfilePage extends StatelessWidget {
  const ProfilePage(this.p, {super.key});
  final Profile p;

  @override
  Widget build(BuildContext context) {
    final label = Theme.of(context).textTheme.labelMedium;
    return ListView(padding: const EdgeInsets.all(12), children: [
      SizedBox(
        height: 72,
        child: FilledButton(
          style: FilledButton.styleFrom(
            backgroundColor: p.paused ? Colors.red.shade700 : const Color(0xFF420000),
            foregroundColor: Colors.white,
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
          ),
          onPressed: () => swarm.setPaused(p, !p.paused),
          child: Text(p.paused ? 'PAUSED — tap to resume' : 'PAUSE',
              style: const TextStyle(fontSize: 22)),
        ),
      ),
      const SizedBox(height: 12),
      Text('VELOCITY ${p.velocity.toStringAsFixed(2)}', style: label),
      Row(children: [
        Expanded(
          child: Slider(
            value: p.velocity,
            onChanged: (v) => swarm.setVelocity(p, v),
            onChangeEnd: (v) => swarm.setVelocity(p, v, last: true),
          ),
        ),
        OutlinedButton.icon(
          icon: const Icon(Icons.restart_alt),
          label: Text('Reset all → ${swarm.defaultVelocity.toStringAsFixed(2)}'),
          onPressed: () => swarm.resetVelocities(p),
        ),
      ]),
      Text('DENSITY ${p.density.toStringAsFixed(2)}', style: label),
      Slider(
        value: p.density,
        onChanged: (v) => swarm.setDensity(p, v),
        onChangeEnd: (v) => swarm.setDensity(p, v, last: true),
      ),
      const SizedBox(height: 8),
      Text('NODES — tap to test', style: label),
      const SizedBox(height: 8),
      GridView.extent(
        maxCrossAxisExtent: 120,
        shrinkWrap: true,
        physics: const NeverScrollableScrollPhysics(),
        mainAxisSpacing: 8,
        crossAxisSpacing: 8,
        children: [for (final n in swarm.nodesOf(p)) NodeTile(p, n)],
      ),
    ]);
  }
}

class NodeTile extends StatelessWidget {
  const NodeTile(this.p, this.n, {super.key});
  final Profile p;
  final Node n;

  @override
  Widget build(BuildContext context) {
    final small = Theme.of(context).textTheme.bodySmall;
    return Material(
      color: n.suspended ? const Color(0xFF4A2A33) : const Color(0xFF2A2A2A),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(8),
        side: BorderSide(color: n.alive ? _green : Colors.grey.shade800, width: 2),
      ),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: () => showModalBottomSheet<void>(
          context: context,
          isScrollControlled: true,
          builder: (_) => ListenableBuilder(
              listenable: swarm, builder: (_, _) => NodeSheet(p, n)),
        ),
        child: Stack(fit: StackFit.expand, children: [
          if (n.battery != null)
            Align(
              alignment: Alignment.bottomCenter,
              child: FractionallySizedBox(
                heightFactor: _batteryLevel(n.battery!),
                widthFactor: 1,
                child: ColoredBox(
                    color: (n.battery! < _batEmpty ? Colors.red : _green).withValues(alpha: 0.18)),
              ),
            ),
          Padding(
            padding: const EdgeInsets.all(6),
            child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              Text('${n.id}',
                  textAlign: TextAlign.center,
                  style: Theme.of(context).textTheme.headlineSmall),
              const Spacer(),
              if (n.fw != null) Text('fw ${n.fw}', style: small),
              if (n.battery != null) Text('${n.battery!.toStringAsFixed(2)} V', style: small),
              if (n.suspended) Text('suspended', style: small?.copyWith(color: _pink)),
              const SizedBox(height: 2),
              LinearProgressIndicator(value: n.velocity, minHeight: 3),
            ]),
          ),
        ]),
      ),
    );
  }
}

// Datel pattern presets (notation: SW/Datel/src/Pattern.h).
const datelPatterns = ['xx_p750AFF_xx_', 'x', 'x_x_x_x_', 'xxxx', 'xFF__x80__x40', 'p7F0AFF'];

// birb presets: Mutator.h preset_patterns.
const birbPatterns = [
  [3, 1, 65, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  [1, 513, 68, 2, 34, 4, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  [520, 0, 63, 514, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  [40, 4, 40, 4, 40, 4, 40, 4, 0, 0, 0, 0, 0, 0, 0, 0],
  [15, 8, 63, 12, 48, 6, 36, 3, 24, 1, 16, 0, 0, 0, 0, 0],
];

class NodeSheet extends StatefulWidget {
  const NodeSheet(this.p, this.n, {super.key});
  final Profile p;
  final Node n;

  @override
  State<NodeSheet> createState() => _NodeSheetState();
}

class _NodeSheetState extends State<NodeSheet> {
  // Peck params shared across nodes for quick A/B testing (ranges: osc.scd).
  static double freq = 14, dur = 2000, curve = 0, amp = 1;
  static final pat = TextEditingController(text: datelPatterns.first);

  @override
  Widget build(BuildContext context) {
    final p = widget.p, n = widget.n;
    final ago = n.lastPing == null
        ? 'never pinged'
        : 'ping ${DateTime.now().difference(n.lastPing!).inSeconds}s ago';
    final info = [
      ago,
      if (n.fw != null) 'fw ${n.fw}',
      if (n.battery != null) '${n.battery!.toStringAsFixed(2)} V',
      if (n.suspended) 'suspended',
    ].join(' · ');

    Widget slider(String name, double v, double min, double max, ValueChanged<double> f,
            {int digits = 1}) =>
        Row(children: [
          SizedBox(width: 110, child: Text('$name ${v.toStringAsFixed(digits)}')),
          Expanded(child: Slider(value: v, min: min, max: max, onChanged: (x) => setState(() => f(x)))),
        ]);

    return SafeArea(
      child: SingleChildScrollView(
        padding: EdgeInsets.fromLTRB(16, 16, 16, 16 + MediaQuery.viewInsetsOf(context).bottom),
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, mainAxisSize: MainAxisSize.min, children: [
          Text('${p.name} ${n.id}', style: Theme.of(context).textTheme.headlineSmall),
          Text(info, style: TextStyle(color: n.alive ? _green : Colors.grey)),
          const SizedBox(height: 12),
          Text('VELOCITY ${n.velocity.toStringAsFixed(2)}'),
          Slider(
            value: n.velocity,
            onChanged: (v) => swarm.setNodeVelocity(p, n, v),
            onChangeEnd: (v) => swarm.setNodeVelocity(p, n, v, last: true),
          ),
          const Divider(),
          if (p.base == '/datel') ...[
            slider('freq Hz', freq, 5, 20, (v) => freq = v),
            slider('dur ms', dur, 50, 5000, (v) => dur = v, digits: 0),
            slider('curve', curve, -10, 10, (v) => curve = v),
            slider('amp', amp, 0, 1, (v) => amp = v, digits: 2),
            FilledButton.icon(
              icon: const Icon(Icons.touch_app),
              label: const Text('PECK'),
              onPressed: () => swarm.peck(p, n.id, freq, dur, curve, amp),
            ),
            const Divider(),
            Wrap(spacing: 6, runSpacing: 6, children: [
              for (final s in datelPatterns)
                ActionChip(label: Text(s), onPressed: () => setState(() => pat.text = s)),
            ]),
            const SizedBox(height: 8),
            Row(children: [
              Expanded(
                child: TextField(
                  controller: pat,
                  maxLength: 224, // MAX_PATTERN_LEN - 1 in SW/Datel/src/Mutator.h
                  decoration: const InputDecoration(labelText: 'pattern', counterText: ''),
                ),
              ),
              const SizedBox(width: 8),
              FilledButton(
                onPressed: () => swarm.pattern(p, n.id, pat.text.trim()),
                child: const Text('PLAY'),
              ),
            ]),
          ] else ...[
            FilledButton.icon(
              icon: const Icon(Icons.music_note),
              label: const Text('TWEET'),
              onPressed: () => swarm.tweet(p, n.id),
            ),
            const SizedBox(height: 8),
            const Text('Set pattern'),
            Wrap(spacing: 6, children: [
              for (final (i, w) in birbPatterns.indexed)
                ActionChip(label: Text('preset ${i + 1}'), onPressed: () => swarm.birbPattern(p, n.id, w)),
            ]),
          ],
        ]),
      ),
    );
  }
}

class SettingsDialog extends StatefulWidget {
  const SettingsDialog({super.key});

  @override
  State<SettingsDialog> createState() => _SettingsDialogState();
}

class _SettingsDialogState extends State<SettingsDialog> {
  final ssid = TextEditingController(text: swarm.ssid);
  final pass = TextEditingController(text: swarm.pass);
  final ip = TextEditingController(text: swarm.manualIp);
  late double vel = swarm.defaultVelocity;

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: const Text('Settings'),
        content: SingleChildScrollView(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            TextField(controller: ssid, decoration: const InputDecoration(labelText: 'Mesh SSID')),
            TextField(
                controller: pass,
                obscureText: true,
                decoration: const InputDecoration(labelText: 'Mesh password')),
            TextField(
                controller: ip,
                decoration: const InputDecoration(
                    labelText: 'Manual node IP', hintText: 'empty = auto (gateway)')),
            const SizedBox(height: 12),
            Text('Default velocity ${vel.toStringAsFixed(2)}'),
            Slider(value: vel, onChanged: (v) => setState(() => vel = v)),
          ]),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
          FilledButton(
            onPressed: () {
              swarm.saveSettings(
                  ssid: ssid.text.trim(),
                  pass: pass.text,
                  manualIp: ip.text.trim(),
                  defaultVelocity: vel);
              Navigator.pop(context);
            },
            child: const Text('Save'),
          ),
        ],
      );
}
