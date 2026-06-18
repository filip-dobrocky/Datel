(
var window, timeLabel, startStopButton, resetButton;
var task;
var isRunning = false;
var elapsed = 0;

// Create GUI elements first
timeLabel = StaticText()
	.align_(\center)
	.font_(Font.default.copy.size_(24))
	.string_(elapsed.asTimeString.drop(-4));

startStopButton = Button()
	.states_([["Start", Color.black, Color.green], ["Stop", Color.white, Color.red]])
	.action_({
		if (isRunning.not) {
			task = Task({
				loop {
					timeLabel.string = elapsed.asTimeString.drop(-4); // removes .000
					1.wait;
					elapsed = elapsed + 1;
				}
			}, AppClock);
			task.start;
			isRunning = true;
			startStopButton.value = 1;
		} {
			task.stop;
			isRunning = false;
			startStopButton.value = 0;
		}
	});

resetButton = Button()
	.states_([["Reset", Color.black, Color.yellow]])
	.action_({
		elapsed = 0;
		timeLabel.string = elapsed.asTimeString.drop(-4);
	});

// Now build the layout using existing elements
window = Window("Stopwatch", Rect(100, 100, 240, 120))
.layout_(
	VLayout(
		timeLabel,
		HLayout(
			startStopButton,
			resetButton
		)
	)
).front;
)


