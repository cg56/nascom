#include <iostream>
#include <string>

using namespace std;


extern void loadNasFile(const string &filename);
extern void setUnbufferedInput();
extern void pollKeyboard();
extern void z80step();


//-------------------------------------------------------------------------
//
// Clear the screen when we're ready to start the emulation.
//
//-------------------------------------------------------------------------

static void clearScreen()
{
	cout << "[2J";
}


//-------------------------------------------------------------------------
//
// Delay between each processor instruction so that the emulation doesn't
// run too fast. You would expect that we want it to run as fast as
// possible, but if we do then the keyboard repeat is way to fast (the
// cursor flashes too fast too).
//
//-------------------------------------------------------------------------

void instructionDelay()
{
	for (int i = 0; i < 2000; ++i)	// Tune this value with an argv ??
		;
}


//-------------------------------------------------------------------------
//
// Let's go!
//
//-------------------------------------------------------------------------

extern "C" unsigned int simz80(unsigned int PC, int count, void (*fnc)()); //??

int main()
{
	loadNasFile("nassys3.nal");
	loadNasFile("nastest.nal");
	loadNasFile("basic.nal");

	clearScreen();
	setUnbufferedInput();

  //simz80(0, 1, pollKeyboard);

	while (1)
	{
		//instructionDelay();
		pollKeyboard();
		z80step();
	}
}

