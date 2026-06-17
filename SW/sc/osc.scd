/*
Datel — OSC control (swarm command helpers + telemetry listeners)

Loaded by main.scd (and osc_test.scd) via this.executeFile. Connect this machine
to the softAP of ANY node — the swarm rebroadcasts targeted commands over the
mesh, so the node you talk to need not be the target.

Ports / addresses (lib/Ecosystem/src/EcosystemConfig.h + src/main.cpp):
  recv (commands -> node) : 54345, base "/datel"
  send (telemetry <- node): 54355, base "/info", softAP-directed broadcast (10.x.y.255)

Exposes the ~helpers (~auto, ~listen, ~pattern, ~peck, ~pause, ~velocity,
~blinking, ~density, ~tempo, ~tempoRand, ~sustain, ~reset) and ~node / ~ip.
*/

(
// --- target node -----------------------------------------------------------
// Auto-detect the node: it is always .1 of the softAP subnet the Wi-Fi adapter
// joined (painlessMesh hands out 10.x.y.1). We unicast to that gateway — a
// 255.255.255.255 broadcast would leave via the default-route (Ethernet)
// interface and never reach the swarm. Re-run this block after reconnecting and
// the address follows the new AP, so there is nothing to edit by hand.
~findNode = {
	var cmd = "powershell -NoProfile -Command \"(Get-NetIPConfiguration | "
		++ "Where-Object { $_.IPv4DefaultGateway.NextHop -like '10.*' } | "
		++ "Select-Object -First 1).IPv4DefaultGateway.NextHop\"";
	var ip = cmd.unixCmdGetStdOut.stripWhiteSpace;
	if (ip.isEmpty) {
		"Datel: no 10.x.y.1 gateway found — is Wi-Fi joined to a node's AP?".warn;
		ip = "10.0.0.1";
	};
	~ip = ip;
	~node = NetAddr(~ip, 54345);
	~ip;
};
~findNode.();

// --- command helpers -------------------------------------------------------
// global emergence master: on(1)/off(0). off => every node stops mutating,
// neighbour-triggering and self-triggering (listen forced off on all nodes).
~auto = { |on = 1| ~node.sendMsg("/datel/auto", on.asInteger) };

// per-node emergence toggle (only meaningful while /auto is 0).
~listen = { |id, on = 1| ~node.sendMsg("/datel/listen", id.asInteger, on.asInteger) };

// make node `id` load a pattern string and play it immediately.
// notation (see src/Pattern.h): "_" rest, "x" hit, "xHH" hit w/ velocity byte,
// "pFDCCAA" peck. e.g. "xx_p750AFF_xx_".
~pattern = { |id, str| ~node.sendMsg("/datel/pattern", id.asInteger, str.asString) };

// make node `id` fire a single peck.
//   freq  Hz     5..20
//   dur   ms     (passed straight through; clamps internal)
//   curve        -10..10  (0 flat, >0 fade out, <0 fade in)
//   amp          0..1 normalised
~peck = { |id, freq = 14, dur = 2000, curve = 0, amp = 1|
	~node.sendMsg("/datel/peck", id.asInteger,
		freq.asFloat, dur.asFloat, curve.asFloat, amp.asFloat)
};

// global sticky stop / resume. while paused, even /pattern and /peck are ignored.
~pause = { |on = 1| ~node.sendMsg("/datel/pause", on.asInteger) };

// master gain for every hit/peck, 0..1.
~velocity = { |v = 1| ~node.sendMsg("/datel/velocity", v.asFloat) };

// enable(1)/disable(0) the LED blink while paused / low battery (default on).
~blinking = { |on = 1| ~node.sendMsg("/datel/blinking", on.asInteger) };

// overall activity density 0..1: scales the idle + suspend times.
//   0 = current values (sparsest, default), 1 = 500 ms floor (densest).
~density = { |d = 0| ~node.sendMsg("/datel/density", d.asFloat) };

// set the knock tempo in BPM (clamped 60..500) across the swarm.
~tempo = { |bpm = 300| ~node.sendMsg("/datel/tempo", bpm.asFloat) };

// enable(1)/disable(0) the per-knock tempo random-walk (default on).
// off => each node locks to its own tempo (pair with ~tempo to hold one BPM).
~tempoRand = { |on = 1| ~node.sendMsg("/datel/tempoRand", on.asInteger) };

// enable(1)/disable(0) sustain: hold the solenoid energized after a finished
// knock (basic knock = quick on/off; sustained = on/off/on-and-held). default off.
~sustain = { |on = 1| ~node.sendMsg("/datel/sustain", on.asInteger) };

// reset pattern + tempo back to firmware defaults on every node.
~reset = { ~node.sendMsg("/datel/reset") };

// --- telemetry listeners (node -> here, on port 54355) ---------------------
thisProcess.openUDPPort(54355);

OSCdef(\ping, { |msg, time, addr|
	// msg[2] = firmware version (0 / nil = pre-versioning firmware) — watch it
	// flip per node during an OTA rollout.
	("ping   <- id" + msg[1] + "fw v" + (msg[2] ? 0)).postln;
}, "/info/ping", recvPort: 54355);

OSCdef(\battery, { |msg, time, addr|
	("battery<- id" + msg[1] + ":" + msg[2].round(0.01) + "V").postln;
}, "/info/battery", recvPort: 54355);

OSCdef(\suspended, { |msg, time, addr|
	("suspend<- id" + msg[1] + ":" + msg[2]).postln;
}, "/info/suspended", recvPort: 54355);

"Datel OSC control ready. Target % :54345".format(~ip).postln;
)
