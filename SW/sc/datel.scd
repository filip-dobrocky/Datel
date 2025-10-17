(
~dir = "/run/media/filip/data/Projects/Datel/SW/sc";

this.executeFile(~dir +/+ "kickBlocks.scd");
this.executeFile(~dir +/+ "patterns.scd");

t = TempoClock(20);
)

(

Pdef(\datel1, Pbind(
	\instrument, \kickBlocks,
	\dur, ~pat.("xxxxxx_______xxx____xxx_x_____x"),

	\out, 0
)).play(t);
)

Pdef(\datel1).stop