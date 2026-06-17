(
// Looping stereo buffer player (field recordings etc.). \amp is lagged for
// smooth live/CC control; gated ASR env so it can fade in and self-free on
// release (~loop.set(\gate, 0)).
SynthDef(\loop, { |out = 0, buf = 0, rate = 1, amp = 1, gate = 1, attack = 1, release = 1|
	var sig = PlayBuf.ar(2, buf, rate * BufRateScale.kr(buf), loop: 1);
	var env = EnvGen.kr(Env.asr(attack, 1, release), gate, doneAction: 2);
	Out.ar(out, sig * env * amp.lag(0.2));
}).add;
)
