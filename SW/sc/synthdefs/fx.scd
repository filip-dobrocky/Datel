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
	Out.ar(out, inSig.blend(sig, \mix.kr(0, 4)));
}).add;

// ---- resonator (DynKlank bank excited by the input) -----------------------
SynthDef(\resoFx, { |out, in, freqs = #[440, 660, 880, 1320], amps = #[1, 1, 1, 1]|
	var inSig = In.ar(in, ~numChans);
	var sig = DynKlank.ar(`[freqs, amps, 1!4], inSig, \freqScale.kr(1), \freqOffset.kr(0), \decayScale.kr(1)) * 0.2;
	Out.ar(out, inSig.blend(Limiter.ar(sig), \mix.kr(0, 4)));
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
)
