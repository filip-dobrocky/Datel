(
// Drone voice: 3 detuned VarSaw partials + a noise-excited Ringz whistle,
// shaped by a self-freeing linen envelope with slow tremolo. Fixed length
// (no gate): lives atk + 1 + rel seconds, then frees itself.
SynthDef(\voice, { |out = 0, freq = 110, amp = 1, pan = 0, atk = 10, rel = 10|
	var sig = Array.fill(3, { |i|
		VarSaw.ar(freq * (i + 1.0001), mul: 0.05 / (i + 1))
	}).sum;
	var sig2 = Ringz.ar(
		WhiteNoise.ar(0.0003),
		TRand.ar(freq, freq * 1.midiratio, Impulse.ar(10))   // wander within a semitone
	);
	var env = EnvGen.kr(Env.linen(atk, 1, rel), doneAction: 2);
	Out.ar(out, Pan2.ar((sig + sig2) * env * amp * (0.8 + SinOsc.kr(0.1, 0, 0.2)), pan));
}).add;
)
