(
SynthDef("pluck", {arg out = 0, pan = 0, amp = 0.1, freq = 440, decay = 5, damp = 0.1, modFreq = 10, modDepth = 0;

	var env, sig;
	env = Env.linen(0, decay, 0).kr(doneAction: 2);
	sig = Pluck.ar(
		in: WhiteNoise.ar(amp),
		trig: Impulse.kr(0),
		maxdelaytime: 0.1,
		delaytime: (freq + SinOsc.ar(modFreq, 0, modDepth)).reciprocal,
		decaytime: decay,
		coef: damp);
	sig = Pan2.ar(sig, pan);
    Out.ar(out, sig);
}).add;
)