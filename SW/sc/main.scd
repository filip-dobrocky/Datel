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
	s.sync;


	// --- OSC swarm control (helpers + telemetry listeners) -----------------
	this.executeFile(~dir +/+ "osc.scd");

	// --- shared tempo clock ------------------------------------------------
	t = TempoClock(1);

	"Datel main: server booted, synthdefs + OSC loaded.".postln;
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
~peck.(3, 16, 2000, 6, 1.0);
~tempo.(120);
~reset.();

~pause.(1)
~pause.(0)

(
Pbindef(\test,
	\freq, Pseq([[n[1], n[3], n[7], n[9]], [n[0], n[3], n[6], n[10]]], inf).midicps,
	\dur, 0.1
).play(t)
)

(
Pbindef(\test,
	\freq, Pseq(~chord.([1, 10, 14]).flatten, inf).midicps * 0.5,
	\dur, 0.25
).play(t)
)

// frenetic glitch: one fixed rhythm cycle, arpeggiating the chord pool
(
Pbindef(\glitch,
	\freq, Pseq(~chord.([10, 13, 9]).flatten, inf).midicps,
	\dur, Pseq(~rhythm.(6, [3, 8], [1/12, 1/16, 1/8], [1.5, 2, 3]), inf)
).play(t)
)

// same, but re-rolls a fresh rhythm every cycle (Plazy + Pn) -> always evolving
(
Pbindef(\glitch,
	\freq, Pseq(~chord.([4, 13, 11]).flatten, inf).midicps,
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
	\freq, Pn(Plazy({ Pseq(~stutter.(~chord.([10, 13, 9]).flatten, 3, [2, 5])) })).midicps,
	\dur, Pseq(~rhythm.(6, [3, 8], [1/12, 1/16, 1/8], [1.5, 2, 3]), inf)
).play(t)
)

(
Pbindef(\test,
	\freq, Pseq(~noteMap, inf).midicps,
	\dur, 0.33
).play
)

(
Pbindef(\test,
	\freq, n.midicps * 0.25,
	\dur, 0.33
).play
)

Pbindef(\test).stop