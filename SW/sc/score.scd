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
	\knock, Pseq([Pn(0, 8),   Pn(1, 3)],   inf),
	\amp,   Pseq([Pn(0.2, 8), Pn(0, 3)],   inf),     // SC silent during the reply
	\datel, Pfunc(~knocker.(dur: 100, mode: \pat, pat: "xa0x_x0f___xff"))
).play(t)
)
Pbindef(\cr).stop;


// 3. ratchet pattern peck prob increasing, play woodpecker pats, fade out pluck

// 4. start voices

// 5. fade in loop, resonator, freak out


// 6. fade out 