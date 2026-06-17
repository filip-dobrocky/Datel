(
SynthDef.new(\pluck, {
	|out=0, freq=440, amp=0.5, gate=1, c3=20, pan=0|
	var env, inp, son, tune, pluck, sost;
	env = Env.new([0,1, 1, 0],[0.005,0.006, 0.0005],[5,-5, -8]);
    inp = amp * LFClipNoise.ar(2000) * EnvGen.ar(env,gate);

	tune = freq.expexp(20.midicps, 100.midicps, 1.003, 1.0001);
	pluck = freq.explin(20.midicps, 100.midicps, 1/3, 1/2);
	sost = freq.expexp(20.midicps, 100.midicps, 2, 0.1);

    son = DWGPlucked2.ar(freq, amp, gate, 4/7, 1/sost, c3, inp, sost, tune);
	son = LPF.ar(son, max(freq * 3, 2000)) * AmpComp.kr(freq, 300, 1/2);
    DetectSilence.ar(son, 0.0001, doneAction:2);
    Out.ar(out, Pan2.ar(son * 0.05, pan));
}).add;
)