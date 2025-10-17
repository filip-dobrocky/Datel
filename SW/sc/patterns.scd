//TODO: accents?
(

~pat = { |string|
	string.collect { |ch| if (ch.asString == "x") { 1 } { Rest(1) } }
};

)

'xx'.collect { |ch| ch == $x };