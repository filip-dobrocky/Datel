/*
Datel — OSC test script

Drives the swarm over OSC and prints telemetry coming back. The OSC init (node
auto-detect, command ~helpers, telemetry listeners) now lives in osc.scd; this
file just loads it and keeps the manual checks below.

Connect this machine to the softAP of ANY node (the swarm rebroadcasts targeted
commands over the mesh, so the node you talk to need not be the target).

Evaluate the block below once, then call the ~helpers or run the demo at the bottom.
*/

(
~dir = thisProcess.nowExecutingPath.dirname;
this.executeFile(~dir +/+ "osc.scd");
)

// ===========================================================================
// Quick manual checks — evaluate lines one at a time.
// ===========================================================================

~auto.(0);                 // freeze the swarm (manual mode)
~listen.(11, 1);            // re-enable emergence on node 11 only
~pattern.(3, "x04x01____xx_");    // node 4 plays this now
~pattern.(3, "x0fxffxxxf0xxx________xf0x____x_x");    // node 4 plays this now
~pattern.(3, "xx_p750AFF_xx_"); // hits + a peck
~peck.(3, 16, 20000, 10, 1.0);   // node 4: single 2 s peck, fade in
~peck.(3, 20, 5000, 1, 1.0);     // node 11: softer peck
~velocity.(1);           // half master gain
~pause.(1);                // silence everything (manual cmds ignored too)
~pause.(0);                // resume
~auto.(1);                 // back to autonomous emergent behaviour

~tempoRand.(0);            // lock tempo (stop the per-knock random walk)
~tempo.(30);              // hold the whole swarm at 120 BPM
~tempoRand.(1);            // resume tempo drift
~density.(1);              // densest (500 ms floor)
~density.(0);              // back to sparsest (default)
~blinking.(0);             // LED dark while paused / low battery
~blinking.(1);             // LED blink back on
~sustain.(1);              // hold solenoid on after each knock
~sustain.(0);              // back to quick on/off (default)
~reset.();                 // pattern + tempo to firmware defaults

// ---------------------------------------------------------------------------
// Demo routine: manual mode, walk a peck across nodes 0..10.
// ---------------------------------------------------------------------------
(
Routine({
	~auto.(0);
	0.5.wait;
	(0..10).do { |id|
		~peck.(id, 16, 1500, 6, 1.0);
		0.1.wait;
	};
	1.0.wait;
	// ~auto.(1);          // hand control back to the swarm
}).play(AppClock);
)




