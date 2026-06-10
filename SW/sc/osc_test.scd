/*
Datel — OSC test script

Drives the swarm over OSC and prints telemetry coming back.

Connect this machine to the softAP of ANY node (the swarm rebroadcasts targeted
commands over the mesh, so the node you talk to need not be the target).

Ports / addresses (from lib/Ecosystem/src/EcosystemConfig.h + src/main.cpp):
  recv (commands -> node) : 54345, base "/datel"
  send (telemetry <- node): 54355, base "/info", softAP-directed broadcast (10.x.y.255)

Evaluate the block below once, then call the ~helpers or run the demo at the bottom.
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

// --- telemetry listeners (node -> here, on port 54355) ---------------------
thisProcess.openUDPPort(54355);

OSCdef(\ping, { |msg, time, addr|
	("ping   <- id" + msg[1]).postln;
}, "/info/ping", recvPort: 54355);

OSCdef(\battery, { |msg, time, addr|
	("battery<- id" + msg[1] + ":" + msg[2].round(0.01) + "V").postln;
}, "/info/battery", recvPort: 54355);

OSCdef(\suspended, { |msg, time, addr|
	("suspend<- id" + msg[1] + ":" + msg[2]).postln;
}, "/info/suspended", recvPort: 54355);

"Datel OSC test ready. Target % :54345".format(~ip).postln;
)

// ===========================================================================
// Quick manual checks — evaluate lines one at a time.
// ===========================================================================

~auto.(0);                 // freeze the swarm (manual mode)
~listen.(11, 1);            // re-enable emergence on node 11 only
~pattern.(4, "xx_xx_");    // node 4 plays this now
~pattern.(4, "x0fxffxxxf0xxx________xf0x____x_x");    // node 4 plays this now
~pattern.(11, "xx_p750AFF_xx_"); // hits + a peck
~peck.(4, 14, 20000, -6, 1.0);   // node 4: single 2 s peck, fade in
~peck.(11, 16, 5000, -3, 1.0);     // node 11: softer peck
~velocity.(1);           // half master gain
~pause.(1);                // silence everything (manual cmds ignored too)
~pause.(0);                // resume
~auto.(1);                 // back to autonomous emergent behaviour

// ---------------------------------------------------------------------------
// Demo routine: manual mode, walk a peck across nodes 0..11.
// ---------------------------------------------------------------------------
(
Routine({
	~auto.(0);
	0.5.wait;
	(0..11).do { |id|
		~peck.(id, rrand(6, 18), 1500, rrand(-8, 8), 1.0);
		0.6.wait;
	};
	1.0.wait;
	~auto.(1);          // hand control back to the swarm
}).play(AppClock);
)
