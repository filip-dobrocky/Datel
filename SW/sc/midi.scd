/*
Datel — MIDI mapping

Loaded by main.scd via this.executeFile (after osc.scd, so ~peck and the note
map `n` exist). MIDI notes trigger one peck per node; CCs set the global peck
parameters and the master level.

Note layout (sequential over the nodes, id = position in ~datelNotes):
    41 42 43 44   57 58 59 60   73 74 75 76   89 90 91 92
    -> node 0 .. 15   (only the first n.size land on a real node)

Notes 76/89/90/91/92 map to nodes 11..15 (which don't exist -> no peck), and
double as toggles for the 5 harmonic chord pools (noteOn flips a pool on/off).
Note 108 = stop all patterns (~stopAll).

CC:
    77  peck freq   (5..20 Hz = knock roll rate)
    78  peck dur    (ms)
    19  reso decay  (decayScale of the global resonator, 0.1..4)
    35  reso nFreqs (0..1 -> 1..n.size partials of the global resonator)
    54  loop amp    (0..1 amp of the woodpecker buffer loop)
    55  reso mix    (0..1 dry/wet of the global resonator)
    79  peck curve  (-10..10)
    80  peck amp    (0..1)
    49  velocity    (0..1 swarm master gain -> OSC /datel/velocity)
    81  density     (0..1 swarm activity density -> OSC /datel/density)
    82  pluck amp   (live level of the pluck submix, via ~pluckGain on ~pluckBus)
    34  voice amp   (live level of all \voice synths, via ~voiceAmp bus)
    83  tempo       (sets OSC swarm BPM; TempoClock t runs at half that BPM)
    84  master level (server volume, as in garden-sc)
*/

// ---- global peck params (live, CC-controlled) -----------------------------
~peckFreq  = 14;
~peckDur   = 300;
~peckCurve = 0;
~peckAmp   = 1;

// master level scaler (same curve as garden-sc)
~midiLvlScale = { |val| val.linexp(0, 127, 0.0005, 4).thresh(0.00051) };

// note -> node id by position; ids past n.size are simply ignored
~datelNotes = [41, 42, 43, 44,  57, 58, 59, 60,  73, 74, 75, 76,  89, 90, 91, 92];

if (MIDIClient.initialized.not) { MIDIClient.init };
MIDIIn.connectAll;

// ---- notes -> pecks -------------------------------------------------------
MIDIdef.noteOn(\datelPeck, { |vel, num|
	var id = ~datelNotes.indexOf(num);
	if (id.notNil and: { id < n.size }) {
		~peck.(id, ~peckFreq, ~peckDur, ~peckCurve, ~peckAmp);
		"peck node % (note %)".format(id, num).postln;
	};
}, ~datelNotes);

// ---- chord-pool toggles (noteOn flips a pool in/out of the global pool) ----
~poolNotes = [76, 89, 90, 91, 92];   // one per ~chordPools group
MIDIdef.noteOn(\poolToggle, { |vel, num|
	var i = ~poolNotes.indexOf(num);
	i !? {
		~poolOn[i] = ~poolOn[i].not;
		~updatePool.();
	};
}, ~poolNotes);

// ---- stop all patterns (note 108) -----------------------------------------
MIDIdef.noteOn(\stopAll, { ~stopAll.(); "STOP ALL PATTERNS".postln }, 108);

// ---- global peck params + master level (CC) -------------------------------
MIDIdef.cc(\datelControls, { |val, num|
	var scaled;
	switch (num)
	{34} {
		scaled = ~midiLvlScale.(val);
		~voiceAmp !? { ~voiceAmp.set(scaled) };   // live level of all \voice synths
		"voice amp: % dB".format(scaled.ampdb.round(0.1)).postln }
	{19} {
		scaled = val.linexp(0, 127, 0.1, 4);
		~reso !? { ~reso.set(\decayScale, scaled) };
		"reso decayScale: %".format(scaled.round(1e-3)).postln }
	{35} {
		scaled = val.linlin(0, 127, 0, 1);
		~reso !? { ~reso.set(\nFreqs, scaled) };
		"reso nFreqs: % (% partials)".format(scaled.round(1e-3), (scaled * (n.size - 1) + 1).round).postln }
	{54} {
		scaled = ~midiLvlScale.(val);
		~loop !? { ~loop.set(\amp, scaled) };
		"loop amp: % dB".format(scaled.ampdb.round(0.1)).postln }
	{55} {
		scaled = val.linlin(0, 127, 0, 1);
		~reso !? { ~reso.set(\mix, scaled) };
		"reso mix: %".format(scaled.round(1e-3)).postln }
	{49} {
		scaled = val.linlin(0, 127, 0, 1);
		~velocity.(scaled);
		"velocity: %".format(scaled.round(1e-3)).postln }
	{77} {
		~peckFreq = val.linlin(0, 127, 5, 20);
		"peck freq: % Hz".format(~peckFreq.round(0.1)).postln }
	{78} {
		~peckDur = val.linexp(0, 127, 30, 4000);
		"peck dur: % ms".format(~peckDur.round(1)).postln }
	{79} {
		~peckCurve = val.linlin(0, 127, -10, 10);
		"peck curve: %".format(~peckCurve.round(0.1)).postln }
	{80} {
		~peckAmp = val.linlin(0, 127, 0, 1);
		"peck amp: %".format(~peckAmp.round(1e-3)).postln }
	{81} {
		scaled = val.linlin(0, 127, 0, 1);
		~density.(scaled);
		"density: %".format(scaled.round(1e-3)).postln }
	{82} {
		scaled = ~midiLvlScale.(val);
		~pluckGain !? { ~pluckGain.set(\amp, scaled) };   // live level of the pluck submix
		"pluck amp: % dB".format(scaled.ampdb.round(0.1)).postln }
	{83} {
		// CC -> swarm BPM; the TempoClock runs at half that BPM (in bps).
		var bpm = val.linexp(0, 127, 60, 500);   // OSC swarm tempo (clamped there)
		t.tempo = bpm / 2 / 60;
		~tempo.(bpm);
		"tempo: swarm % BPM, clock % BPM".format(bpm.round(0.1), (bpm / 2).round(0.1)).postln }
	{84} {
		scaled = ~midiLvlScale.(val);
		s.volume.volume = scaled.ampdb;
		"master: % dB".format(scaled.ampdb.round(0.1)).postln }
	{};
});

"Datel MIDI mapping loaded.".postln;
