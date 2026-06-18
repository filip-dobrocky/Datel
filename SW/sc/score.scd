//--- 1. auto off, play different patterns on different woodpeckers gradually, turn on listen, then auto on, play with density
~reset.()
~auto.(0)


~pattern.(6, "pF30AFF___x05xff_");
~pattern.(8, "x___p7F09FF_xf0x6fx_");
~pattern.(7, "pCA99FF_xf0x6fx_");

~listen.([6, 7, 8, 4, 9, 10], 1);

~auto.(1)

// density + velocity

~auto.(0)


//--- 2. short drone swell, call and response with pluck pattern
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


(
Pbindef(\cr,
	\instrument, \pluck,
	\out, ~pluckBus,
	\freq,  Pn(Plazy({ Pseq(~chord.(~chordPool).flatten) })).midicps,
	\dur,   Pseq([Pn(1/8, 8), Pn(1/2, 3)], inf),     // fast call, slower reply
	// \dur,   Pseq([Pn(1/8, 8), Pn(1/2, 3)], inf) * 0.5,     // fast call, slower reply
	\knock, Pseq([Pn(0, 8),   Pn(1, 3)],   inf),
	\amp,   Pseq([Pn(0.2, 8), Pn(0, 3)],   inf),     // SC silent during the reply
	\datel, Pfunc(~knocker.(prob: 0.5, dur: 100, mode: \pat, pat: "xa0x_x0f___xff"))
).play(t)
)

(
Pbindef(\hopcip,
	\instrument, \pluck,
	\freq, Pseq([[n[1], n[3], n[7], n[9]], [n[0], n[3], n[6], n[10]]], inf).midicps * 0.25,
	\dur, 0.125,
	\damp, Pexprand(1, 0.001),
	\modFreq, Pkey(\freq) * 2,
	\modDepth, Pseq([0, 1, 10, 20], inf),
	\decay, Pn(Pgeom(1.0, 0.8, 8)),
	\pan, Pwhite(-0.33, 0.33),
	\datel, Pfunc(~knocker.(prob: 0)),
	// \datel, Pfunc(~knocker.(prob: 0.1)),
	\amp, 0.17,

	\out,   ~pluckBus,
).play(t)
)

(
Pbindef(\cr).stop;
Pbindef(\voices).stop;
Pbindef(\hopcip).stop;
)


// 3. ratchet pattern peck prob increasing, play woodpecker pats, fade out pluck


(
Pdef(\ratchet, Pn(Plazy({
	var r = ~ratchet.(~chord.(~chordPool).flatten.scramble, [1/4, 1/8, 1/2], 0.4, [2, 6]);
	Pbind(
		\instrument, \pluck,
		\out, ~pluckBus,
		\freq, Pseq(r[0], 1).midicps,
		\dur,  Pseq(r[1], 1),
		\pan,  Pwhite(-1, 1),
		\amp,    Pwhite(-18, -8).dbamp * -12.dbamp,
		\damp, Pexprand(1, 0.001),
		\modFreq, Pkey(\freq) * 2,
		\modDepth, Pseq([0, 1, 5, 10], inf),
		\decay, Pn(Pgeom(1.5, 0.9, 8)),
		\pan, Pwhite(-0.33, 0.33),
		\datel, Pfunc({ |ev| ~knocker.(prob: 1, peckAmp: ev[\amp]).(ev) })
	)
}))).play(t)
)

(
Pbindef(\ratchet).stop;
Pdef(\fxRatchet, PbindFx(
	Pn(Plazy({
		var r = ~ratchet.(~chord.(~chordPool).flatten.scramble, [1/4, 1/8, 1/2, 1/16], 0.5, [2, 6]);
		Pbind(
			\instrument, \pluck,
			\out,    ~pluckBus,
			\freq,   Pseq(r[0], 1).midicps * 0.25,
			\dur,    Pseq(r[1], 1),
			\amp,    Pwhite(-18, -8).dbamp * -6.dbamp,
			\pan,    Pwhite(-0.7, 0.7),
			\damp,   Pexprand(1, 0.001),
			\modFreq, Pkey(\freq) * 2,
			\modDepth, Pseq([0, 1, 5, 10], inf),
			\decay, Pn(Pgeom(1.5, 0.9, 8)),
			// fx only on slots longer than 0.15 beat; short ratchets stay dry (0 = no fx)
			\fxOrder, Pfunc({ |ev|
				if (ev[\dur] > 0.1) { [[1], [2], [3], [4], [2, 1], [3, 1]].choose } { [0, [4]].choose } }),
			\cleanupDelay, 0.3,
			\datel, Pfunc({ |ev| ~knocker.(prob: 0.1, dur: rrand(300, 1000), peckAmp: ev[\amp]).(ev) })
		)
	})),
	[
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
		\crush, Prand([0.2, 0.3, 0.5], inf),
		\bass, Prand([0, 0.5, 0.66], inf),
		\cleanupDelay, Pkey(\dur)
	], [
		\fx, \delay,
		\mix, Prand([0.2, 0.5, 0.7], inf),
		\feedback, Pwhite(0.1, 0.7),
		\delayTime, Pkey(\dur) * Prand([0.5, 0.25, 0.125], inf),
		\cleanupDelay, Pkey(\delayTime) * 10 * Pkey(\feedback)
	], [
		\fx, \pitchShifterFx,
		\mix, Prand([0.2, 0.5, 0.7], inf),
		\ratio, Prand([2, 3, 4, 6], inf),
		\cleanupDelay, 0.8
	]
)).play(t);
)
Pdef(\fxRatchet).stop;

// --- 4. start voices, fade in loop, resonator, freak out
Pbindef(\voices).play

(
~loop = Synth(\loop, [\out, ~resoBus, \buf, ~loopBuf, \amp, 0.3, \attack, 5, \decay, 5]);
)

~auto.(1)

// 5. fade out

~loop.set(\gate, 0)

~auto.(0);

~pause.(1);

