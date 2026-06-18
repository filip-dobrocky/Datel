/*
Datel — FX SynthDefs (stereo)

PbindFx-compatible insert fx ported from garden-sc, mono -> `~numChans` (stereo).
PbindFx convention (miSCellaneous_lib): each fx has positional `out` / `in` args,
reads with In.ar(in, ...), and writes its dry/wet blend with a single Out.ar(out, ..)
(NOT ReplaceOut — PbindFx pre-zeroes each routing bus and expects fx to add into it).
\delay is the same shape with its own feedback loop.
*/

~numChans = 2;

(
// ---- reverb (JPverb + optional shimmer) -----------------------------------
SynthDef(\reverbFx, { |out, in|
	var inSig = In.ar(in, ~numChans);
	var mix   = \mix.kr(0, 4);
	var wet   = DelayN.ar(inSig, 0.3, \predelay.kr(0.0));
	wet = JPverb.ar(wet, \decay.kr(0.9), \damping.kr(0.75));
	wet = wet.blend(
		PitchShift.ar(wet, pitchRatio: 2 ** (\shimmerPitch.kr(12) / 12), pitchDispersion: 0.01, timeDispersion: 0.1),
		\shimmerMix.kr(0, 5)
	);
	Out.ar(out, inSig.blend(wet, mix));
}).add;

// ---- destroyer (decimate / crush + bass LPF) ------------------------------
SynthDef(\destroyerFx, { |out, in|
	var inSig = In.ar(in, ~numChans);
	var crush = \crush.kr(0, 1.5).clip(0, 1) ** 0.5;
	var bass  = \bass.kr(0, 0.5).clip(0, 1);
	var sig   = Decimator.ar(
		inSig,
		s.sampleRate * (1 - crush.linlin(0, 1, 0, 0.8)),
		crush.linlin(0, 1, 24, 2)
	);
	sig = ((1 - bass) * sig) + (bass * RLPF.ar(sig, \cutoff.kr(300), 0.5));
	sig = sig.softclip;
	sig = LeakDC.ar(sig);
	sig = inSig.blend(sig, \mix.kr(0, 4));
	sig = sig * EnvGen.kr(Env.asr(\atk.kr(0.01), 1, \rel.kr(0.5)), \gate.kr(1), doneAction: Done.freeSelf);
	Out.ar(out, sig);
}).add;

// ---- pitch shifter --------------------------------------------------------
SynthDef(\pitchShifterFx, { |out, in|
	var inSig = In.ar(in, ~numChans);
	var sig = PitchShift.ar(inSig, 0.2, \ratio.kr(1), 0.005, 0.1);
	Out.ar(out, inSig.blend(sig, \mix.kr(0, 4)));
}).add;

// ---- delay (filtered feedback) --------------------------------------------
SynthDef(\delay, {
	arg in = 0, out = 0, amp = 1, delayTime = 0.3, maxDelayTime = 5, feedback = 0.6,
	filterType = 0, cutoff = 20000, rq = 1, lagTime = 5;
	var inSig, lowPass, highPass, bandPass, filter, localIn, delay, sig, mix;
	inSig = In.ar(in, ~numChans);
	localIn = LocalIn.ar(~numChans);

	feedback = feedback.lag(1);

	lowPass = RLPF.ar(localIn, cutoff, rq);
	highPass = RHPF.ar(localIn, cutoff, rq);
	bandPass = BPF.ar(localIn, cutoff, rq);
	filter = Select.ar(filterType, [localIn, lowPass, highPass, bandPass]);

	delay = DelayC.ar(inSig + (filter * feedback), maxDelayTime, delayTime.lag2(lagTime));
	LocalOut.ar(delay);

	sig = delay * amp;

	mix = \mix.kr(0, 5);
	sig = (1 - mix) * inSig + (sig * mix);

	Out.ar(out, sig);
}).add;

// ---- resonator (bank tuned to the note map) -------------------------------
// Reads its input bus, rings it through partials taken from the note map `n`,
// blends dry/wet by \mix and writes to \out. \nFreqs (0..1) fades partials in
// one by one, 1 partial at 0 .. all numFreqs at 1. Fed only by the loop.
SynthDef(\reso, { |in = 0, out = 0|
	var inSig    = In.ar(in, ~numChans);
	var freqs    = n.midicps;                          // partials from the note map
	var numFreqs = freqs.size;                         // max active (= n.size)
	var nNorm    = \nFreqs.kr(0, 0.1).clip(0, 1);      // 0..1
	var count    = nNorm * (numFreqs - 1) + 1;         // 1..numFreqs (continuous)
	var amps     = numFreqs.collect { |i| (count - i).clip(0, 1) }; // fade each in
	var sig = DynKlank.ar(`[freqs, amps, 1 ! numFreqs], inSig, \freqScale.kr(1), \freqOffset.kr(0), \decayScale.kr(1)) * 0.2;
	sig = Limiter.ar(sig);
	Out.ar(out, inSig.blend(sig, \mix.kr(0, 4)));
}).add;

// ---- master limiter (in-place on the master bus, last in the chain) -------
SynthDef(\masterLimiter, { |bus = 0|
	var sig = In.ar(bus, ~numChans);
	ReplaceOut.ar(bus, Limiter.ar(sig, \level.kr(0.95), \dur.kr(0.01)));
}).add;
)
