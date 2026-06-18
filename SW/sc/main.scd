/*
Datel — main script: sound synthesis + OSC control.

Evaluate the boot block below once. It boots the server, loads the SynthDefs and
pattern helper, and wires up the OSC swarm control (osc.scd) — so afterwards both
the synthesis Pdefs and the ~OSC helpers (~auto, ~pattern, ~peck, ~tempo, …) are
available. Then run the synthesis / OSC blocks further down, one at a time.
*/

(
~dir = thisProcess.nowExecutingPath.dirname;
s.options.memSize = 2.pow(20);
s.options.numWireBufs = 1024;
s.options.numPrivateAudioBusChannels = 1024;

s.waitForBoot({
	// ---- note map ---------------------------------------------------------
	n = [
		66, //  0 - F#4
		88, //  1 - E6
		93, //  2 - A6
		67, //  3 - G4
		79, //  4 - G5
		78, //  5 - F#5
		81, //  6 - A5
		84, //  7 - C6
		90, //  8 - F#6
		83, //  9 - B5
		62  // 10 - D4
	];
	// ---- chords -----------------------------------------------------------
	c = [ // indices of note map (diatonic in G major)
		[1, 3, 7, 9],    // 0 -  Cmaj7 - C E G B
		[0, 3, 6, 10],   // 1 - Dadd4 - D F# G A
		[3, 9, 10],      // 2 - G     - G B D
		[1, 3, 9, 10],   // 3 - Em7   - E G B D
		[5, 6, 10],      // 4 - D     - D F# A
		[1, 3, 6, 7],    // 5 - Am7   - A C E G
		[5, 9, 10],      // 6 - Bm    - B D F#
		[3, 8, 9, 10],   // 7 - Gmaj7 - G B D F#
		[1, 3, 7],       // 8 - C     - C E G
		// --- less typical: clusters, quartal, add/sus, slash ---
		[4, 5, 6],       // 9 - cluster  - F# G A (whole-tone cluster)
		[7, 1, 3, 6, 10],// 10 - C6/9     - C E G A D
		[3, 6, 9, 10],   // 11 - Gadd9    - G A B D
		[3, 7, 10],      // 12 - Csus2    - C D G
		[1, 3, 5, 9, 10],// 13 - Em9      - E G B D F#
		[1, 5, 9],       // 14 - Esus2    - E F# B
		[1, 3, 7, 10]    // 15 - C/D      - C E G over D (slash/polychord)
	];

	// ---- chord helper -----------------------------------------------------
	// ~chord.(i) -> MIDI notes of chord i; omit i for all chords as note arrays
	~chord = { |i|
		i.isNil.if(
			{ c.collect({ |ch| ch.collect({ |idx| n[idx] }) }) },
			{ c[i].collect({ |idx| n[idx] }) }
		);
	};

	// ---- chord pools: 5 harmonic groupings of c (each = indices into c) ----
	~chordPools = [
		[2, 7, 11],       // 0 - tonic        G  Gmaj7  Gadd9
		[0, 8, 10, 12],   // 1 - subdominant  Cmaj7  C  C6/9  Csus2
		[1, 4],           // 2 - dominant     Dadd4  D
		[3, 5, 6, 13],    // 3 - minor        Em7  Am7  Bm  Em9
		[9, 14, 15]       // 4 - color        cluster  Esus2  C/D
	];
	// ~chordPool is the live global pool the patterns draw from. Toggling pools
	// (MIDI notes 76/89/90/91/92) updates it: none on -> pool 0, else the union.
	~poolOn = Array.fill(~chordPools.size, false);
	~chordPool = ~chordPools[0];
	~updatePool = {
		var on = (0 .. ~chordPools.size - 1).select { |i| ~poolOn[i] };
		~chordPool = on.isEmpty.if(
			{ ~chordPools[0] },
			{ on.collect { |i| ~chordPools[i] }.flatten }
		);
		"chordPool: pools % -> chords %".format(on.isEmpty.if({ [0] }, { on }), ~chordPool).postln;
	};

	// ~restDensity (0..1) scales the rests in ~rhythm: 0 = full rests, 1 = none.
	// Set it live (~restDensity = 0.7) to thin/thicken re-rolling rhythms.
	// (Note: ~density is the swarm OSC control from osc.scd — different thing.)
	~restDensity = 0;

	// ---- rhythm helper ----------------------------------------------------
	// ~rhythm.() -> frenetic/glitchy dur sequence (in beats, for TempoClock):
	// short clumps of fast notes (subdivs, incl. tuplets) split by longer Rests.
	//   clumps  : number of bursts in the sequence
	//   size    : [min, max] notes per burst (random per burst)
	//   subdivs : pool of fast note durations, picked per note (1/12, 1/6 = tuplets)
	//   rests   : pool of gap lengths, inserted as silent Rests between bursts
	//   density : 0..1, scales rests by (1 - density); defaults to ~restDensity.
	//             1 drops rests entirely -> continuous burst.
	// Wrap in Plazy + Pn to re-roll the rhythm every cycle (see patterns below).
	~rhythm = { |clumps = 4, size, subdivs, rests, density|
		var seq = [], rest;
		size    = size    ? [2, 6];
		subdivs = subdivs ? [1/8, 1/12, 1/16, 1/6];   // 1/12, 1/6 = tuplets
		rests   = rests   ? [1, 1.5, 2, 3];
		density = (density ? ~restDensity ? 0).clip(0, 1);
		clumps.do {
			rrand(size[0], size[1]).do { seq = seq.add(subdivs.choose) };
			rest = rests.choose * (1 - density);
			(rest > 0).if { seq = seq.add(Rest(rest)) };  // skip when density = 1
		};
		seq;
	};

	// ---- stutter helper ---------------------------------------------------
	// ~stutter.(seq) -> copy of seq with random notes repeated in place.
	//   seq   : pitch (or any) array to mangle
	//   count : how many spots to stutter
	//   reps  : [min, max] copies the chosen note is replaced with
	// Wrap in Plazy + Pn for fresh stutters every cycle (see patterns below).
	~stutter = { |seq, count = 1, reps|
		reps = reps ? [2, 4];
		seq = seq.asArray.copy;
		count.do {
			var i = seq.size.rand;
			var note = seq[i];
			seq = seq.keep(i) ++ Array.fill(rrand(reps[0], reps[1]), note) ++ seq.drop(i + 1);
		};
		seq;
	};

	// --- SynthDefs ---------------------------------------------------------
	this.executeFile(~dir +/+ "synthdefs/pluck.scd");
	this.executeFile(~dir +/+ "synthdefs/fx.scd");
	this.executeFile(~dir +/+ "synthdefs/voice.scd");
	this.executeFile(~dir +/+ "synthdefs/loop.scd");

	// --- sample buffers (s.sync below waits for the load) ------------------
	~loopBuf = Buffer.read(s, ~dir +/+ "woodpeckers-loop.wav");

	s.sync;


	// --- OSC swarm control (helpers + telemetry listeners) -----------------
	this.executeFile(~dir +/+ "osc.scd");

	// --- shared tempo clock ------------------------------------------------
	t = TempoClock(1);

	// ---- node bridge: drive physical nodes from SC ------------------------
	// c[i] are node ids; pitch of node id = n[id]. (osc.scd's ~peck / ~pattern
	// are loaded just above.) Low-level wrappers, id arrays.
	// Multi-node sends MUST be spaced: firing several /pattern|/peck back-to-back
	// floods the node->mesh rebroadcast and all but the first get dropped. Each
	// call forks on SystemClock so `gap` is in SECONDS wherever it's invoked.
	~nodePeck = { |ids, freq = 14, dur = 250, curve = 0, amp = 1, gap = 0.05|
		{ ids.asArray.do { |id| ~peck.(id, freq, dur, curve, amp); gap.wait } }.fork(SystemClock);
	};
	~nodePat  = { |ids, str = "x", gap = 0.05|
		{ ids.asArray.do { |id| ~pattern.(id, str); gap.wait } }.fork(SystemClock);
	};

	// pitch -> node id, matched by pitch class (octave-robust), nearest octave.
	~nodeOf = { |midi|
		var pc = midi.round.asInteger % 12;
		var cands = (0 .. n.size - 1).select { |i| n[i] % 12 == pc };
		cands.isEmpty.if({ nil }, { cands.minItem { |i| (n[i] - midi).abs } });
	};

	// ~knocker -> a per-event function to plug into ANY Pbind/Pbindef via Pfunc:
	//   Pbindef(\x, ..., \datel, Pfunc(~knocker.(prob: 0.4)))   // keep it LAST
	// One OSC action per event: a chord (freq array) knocks all its nodes, a
	// single note knocks one, a rest knocks nothing. A per-event \knock (0/1)
	// gates on top of `prob`, so a pattern can sequence which events reach the
	// swarm (e.g. silence SC with \amp 0 while \knock stays 1 -> node-only reply).
	//   prob : 0..1 chance an eligible event fires nodes
	//   dur  : peck length (ms)      mode : \peck (default) or \hit
	~knocker = { |prob = 1, dur = 200, mode = \peck, peckFreq = 15, peckAmp = 1, pat = "x", amp = 1|
		{ |ev|
			var rest = (ev[\type] == \rest) or: { ev[\dur].isRest };
			var gate = (ev[\knock] ? 1) * prob;
			// ?? is lazy (? would eagerly eval the fallback -> nil.midicps error).
			var freq = ev[\freq] ?? { ev[\midinote] !? (_.midicps) };
			(rest.not and: { freq.notNil } and: { gate.coin }).if {
				var ids = freq.asArray.collect { |f| ~nodeOf.(f.cpsmidi) }.reject(_.isNil).as(Set).asArray;
				(mode == \peck).if(
					{ ~nodePeck.(ids, peckFreq, dur, 0, amp) },
					{ ~nodePat.(ids, pat) });
			};
			ev;   // must never return nil (nil would end the pattern)
		};
	};

	// --- master chain ------------------------------------------------------
	// Only the loop feeds the resonator: loop -> ~resoBus -> reso -> bus 0.
	// Everything else writes straight to bus 0; the limiter sits last (tail).
	~resoBus = Bus.audio(s, ~numChans);
	~reso    = Synth(\reso, [\in, ~resoBus, \out, 0, \mix, 0], addAction: 'addToTail');
	~limiter = Synth(\masterLimiter, [\bus, 0], addAction: 'addToTail');

	// global level for all \voice synths: map their \gAmp to this control bus.
	~voiceAmp = Bus.control(s, 1).set(1);

	// --- MIDI mapping (notes -> pecks, CC -> peck params + master) ----------
	this.executeFile(~dir +/+ "midi.scd");

	this.executeFile(~dir +/+ "stopwatch.scd");

	"Datel main: server booted, synthdefs + OSC + MIDI loaded.".postln;
	1.wait;
	~auto.(0);                 // freeze the swarm (manual mode)
});
)


// ===========================================================================
// OSC control — drive the swarm (helpers from osc.scd). Evaluate one at a time.
// ===========================================================================
~findNode.();              // re-detect the node after reconnecting Wi-Fi
~auto.(0);                 // freeze the swarm (manual mode)
~auto.(1);                 // back to autonomous emergent behaviour
~pattern.(3, "xx_p750AFF_xx_");
~peck.(, 16, 2000, 6, 1.0);
~tempo.(120);
~reset.();

~density.(0)

~pause.(1)
~pause.(0)

(
Pbindef(\test,
	\freq, Pseq([[n[1], n[3], n[7], n[9]], [n[0], n[3], n[6], n[10]]], inf).midicps,
	\dur, 0.1,
	\datel, Pfunc(~knocker.(prob: 0.8))
).play(t)
)

(
Pbindef(\test,
	\freq, Pn(Plazy({ Pseq(~chord.(~chordPool).flatten) })).midicps * 0.5,
	\dur, 0.25
).play(t)
)

// frenetic glitch: one fixed rhythm cycle, arpeggiating the chord pool
(
Pbindef(\glitch,
	\freq, Pn(Plazy({ Pseq(~chord.(~chordPool).flatten) })).midicps,
	\dur, Pseq(~rhythm.(6, [3, 8], [1/12, 1/16, 1/8], [1.5, 2, 3]), inf)
).play(t)
)

// same, but re-rolls a fresh rhythm every cycle (Plazy + Pn) -> always evolving
(
Pbindef(\glitch,
	\freq, Pn(Plazy({ Pseq(~chord.(~chordPool).flatten) })).midicps,
	\dur, Pn(Plazy({ Pseq(~rhythm.(6, [3, 8], [1/12, 1/16, 1/8], [1.5, 2, 3])) }))
).play(t)
)

Pbindef(\glitch).stop

// rest density (0 = sparse/full rests, 1 = no rests) — tweak live while playing
~restDensity = 0;
~restDensity = 0.5;
~restDensity = 1;

// stutter: repeat random notes of the chord pool, re-rolled every cycle
(
Pbindef(\glitch,
	\instrument, \pluck,
	\freq, Pn(Plazy({ Pseq(~stutter.(~chord.(~chordPool).flatten, 3, [2, 5])) })).midicps,
	\dur, Pseq(~rhythm.(6, [3, 8], [1/12, 1/16, 1/8], [1.5, 2, 3]), inf),
	\pan, Pwhite(-1, 1)
).play(t)
)

(
Pbindef(\test,
	\instrument, \pluck,
	\freq, Pseq(n, inf).midicps * 1,
	\dur, 0.33
).play
)

// drone: bursts of ~10 sustained voices (80% hit), then an 18s gap, forever.
// Voices self-free, so they ring while the pattern moves on (was a Tdef).
(
Pbindef(\voices,
	\instrument, \voice,
	\dur,   Pseq([Pexprand(0.1, 1.0, 10), 18], inf),         // 10 onsets, then a gap
	\type,  Pseq([Pwrand([\note, \rest], [0.8, 0.2], 10), \rest], inf),
	\freq,  (Prand(n, inf)).midicps * 0.25,                   // low drones
	\pan,   Pwhite(-0.5, 0.5),
	\atk,   Pwhite(5, 13),
	\rel,   Pwhite(8, 14),
	\amp,   1,
	\gAmp,  ~voiceAmp.asMap          // all voices follow the ~voiceAmp control bus
).play(t)
)
Pbindef(\voices).stop

// woodpecker loop — plays ~loopBuf into ~resoBus (the only reso input); CC 54 amp
(
~loop = Synth(\loop, [\out, ~resoBus, \buf, ~loopBuf, \amp, 0.3, \attack, 5, \decay, 5]);
)
~loop.set(\gate, 0);   // fade out + free

(
Pbindef(\test,
	\freq, n.midicps * 0.25,
	\dur, 0.33
).play
)

Pbindef(\test).stop


// ===========================================================================
// SC <-> physical node interaction (needs ~findNode.() + ~auto.(0)).
// Drop ~knocker into ANY Pbindef as a Pfunc on a spare key (\datel), placed
// LAST so \freq/\dur/\amp are resolved when it reads them. One OSC action per
// event: chord -> all its nodes, note -> one, rest -> none. Per-event \knock
// (0/1) gates which events reach the swarm. ~auto.(1) lets knocks ripple.
// ===========================================================================

// bridge smoke test
~pattern.(3, "x");                 // single clean knock on node 3
~nodePat.([3, 9, 10], "x");        // nodes 3/9/10 each knock once (spaced ~50ms)
~nodePeck.([3], 14, 300);          // node 3: one peck = a short ROLL of knocks

t.tempo=1

// DOUBLING — plug into the glitch line; some notes also knock their node.
// prob keeps the solenoids sane on fast bursts; crank it toward 1 to double all.
(
Pbindef(\glitch,
	\instrument, \pluck,
	\freq, Pn(Plazy({ Pseq(~stutter.(~chord.(~chordPool).flatten, 3, [2, 5])) })).midicps *0.5,
	\dur, Pseq(~rhythm.(6, [3, 8], [1/8, 1/6], [1.5, 2, 3]), inf),
	\datel, Pfunc(~knocker.(prob: 1, dur: 1))
).play(t)
)
Pbindef(\glitch).stop;

// CALL & RESPONSE — one line, alternating phases: SC burst (knock off, amp up)
// then node reply (knock on, amp 0 -> SC silent). Tune the Pn run-lengths.
(
Pbindef(\cr,
	\instrument, \pluck,
	\freq,  Pn(Plazy({ Pseq(~chord.(~chordPool).flatten) })).midicps,
	\dur,   Pseq([Pn(1/8, 8), Pn(1/2, 3)], inf),     // fast call, slower reply
	\knock, Pseq([Pn(0, 8),   Pn(1, 3)],   inf),
	\amp,   Pseq([Pn(0.2, 8), Pn(0, 3)],   inf),     // SC silent during the reply
	\datel, Pfunc(~knocker.(dur: 100, mode: \pat, pat: "xx_x___x"))
).play(t)
)
Pbindef(\cr).stop;

// COUNTERPOINT — hocket: each step is EITHER an SC note OR a node knock.
(
Pbindef(\cp,
	\instrument, \pluck,
	\freq,  Pn(Plazy({ Pseq(~chord.(~chordPool).flatten) })).midicps,
	\dur,   Pseq([1/8, 1/8, 1/4], inf),
	\knock, Pseq([0, 0, 1], inf),                    // every 3rd step -> node
	\amp,   Pseq([0.18, 0.18, 0], inf),              // SC silent on the node step
	\datel, Pfunc(~knocker.(dur: 120))
).play(t)
)
Pbindef(\cp).stop;


// ===========================================================================
// PbindFx test (miSCellaneous_lib) — pluck source + per-event fx chain, and the
// same notes drive the physical nodes via ~knocker. \fx names a SynthDef from
// synthdefs/fx.scd; \fxOrder selects/sequences them (1-based):
//   1 reverbFx   2 destroyerFx   3 delay   4 pitchShifterFx
// ===========================================================================
Pdef.clear
(
Pdef(\fxPluck, PbindFx([
	\instrument, \pluck,
	// \dur,   Prand([1, 0.5, 0.25], inf),
	\dur, Pseq(~rhythm.(6, [3, 8], [1/8, 1/6], [1.5, 2, 3]), inf),

	\amp,   Pwhite(0.5, 0.8, inf),
	\amp,   Pseq([0.5, 0.5, 0], inf),              // SC silent on the node step
	// \freq,  Pseq(~chord.([2, 12, 10]).flatten, inf).midicps * 0.5,
	\freq, Pn(Plazy({ Pseq(~stutter.(~chord.(~chordPool).flatten, 3, [2, 5])) })).midicps *0.5,
	\pan,   Pwhite(-0.7, 0.7),
	\knock, Pseq([Pn(0, 8),   Pn(1, 3)],   inf),
	// \c3,    Prand([10, 20, 40], inf),
	\out,   0,
	\fxOrder, Prand([[2], [3], [4], [2, 1], [3, 1], [4, 1]], inf),
	// \fxOrder, [4],
	\cleanupDelay, 0.1,                     // pluck rings out; allow the tail
	\datel, Pfunc(~knocker.(prob: 1, dur: rrand(500, 155))),     // notes also knock the swarm
	\datel, Pfunc(~knocker.(dur: 100, mode: \pat, pat: "xx_x___x")),

], [
	\fx, \reverbFx,
	\mix, Pseq([0.2, 0.5, 0.7, 0.9], inf),
	\decay, Prand([1, 2, 4], inf),
	\predelay, 0.2,
	\damping, Prand([0.82, 0.99], inf),
	\shimmerMix, Pseq([0, 0.5, 1], inf),
	\cleanupDelay, Pkey(\decay) + 1
], [
	\fx, \destroyerFx,
	\mix, Prand([0.2, 0.5, 0.7], inf),
	\crush, Prand([0.2, 0.3, 0.5, 0.84], inf),
	\bass, Prand([0, 0.5, 0.66], inf),
	\cleanupDelay, 1
], [
	\fx, \delay,
	\mix, Prand([0.2, 0.5, 0.7], inf),
	\feedback, Pwhite(0.1, 0.7),
	\delayTime, Pkey(\dur) * Prand([0.5, 0.25, 0.125], inf),
	\cleanupDelay, Pkey(\delayTime) * 10 * Pkey(\feedback)
], [
	\fx, \pitchShifterFx,
	\mix, Prand([0.2, 0.5, 0.7], inf),
	\ratio, Prand([0.5, 2, 3, 4, 6], inf),
	\cleanupDelay, 0.8
])).play(t);
)
Pdef(\fxPluck).stop;

// stop everything (panic)
(
[\test, \glitch, \cr, \cp].do { |key| Pbindef(key).stop };
Pdef(\fxPluck).stop;
)

MIDIIn.connectAll
MIDIFunc.trace(true)
