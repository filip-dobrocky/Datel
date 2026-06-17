/*
Datel — MIDI mapping

Loaded by main.scd via this.executeFile (after osc.scd, so ~peck and the note
map `n` exist). MIDI notes trigger one peck per node; CCs set the global peck
parameters and the master level.

Note layout (sequential over the nodes, id = position in ~datelNotes):
    41 42 43 44   57 58 59 60   73 74 75 76   89 90 91 92
    -> node 0 .. 15   (only the first n.size land on a real node)

CC:
    77  peck freq   (5..20 Hz = knock roll rate)
    78  peck dur    (ms)
    79  peck curve  (-10..10)
    80  peck amp    (0..1)
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

// ---- global peck params + master level (CC) -------------------------------
MIDIdef.cc(\datelControls, { |val, num|
	var scaled;
	switch (num)
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
	{84} {
		scaled = ~midiLvlScale.(val);
		s.volume.volume = scaled.ampdb;
		"master: % dB".format(scaled.ampdb.round(0.1)).postln }
	{};
});

"Datel MIDI mapping loaded.".postln;
