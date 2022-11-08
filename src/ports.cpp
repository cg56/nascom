#include <cstdio>
#include <queue>
#include <stdint.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <iostream> //??

using namespace std;

extern void instructionDelay();


//-------------------------------------------------------------------------
//
// The NASCOM keyboard does not deliver ASCII characters. Instead the
// various keys are arranged in an 8x8 matrix organization. Each row is
// scanned by the keyboard driver (outputting to port 0) and the column
// bits are read from port 0 too.
//
//-------------------------------------------------------------------------

static uint8_t keyMatrix[9] =	{0};
static uint8_t keyRow = 0;


//-------------------------------------------------------------------------
//
// Handle the port OUT instruction.
//
//-------------------------------------------------------------------------

extern "C"  //??
void portOut(uint8_t port, uint8_t value)
{
  static uint8_t prevPort = 0;
  uint8_t highToLow;

  switch (port) {
  case 0:   // Port 0 is for driving the keyboard rows
    highToLow = prevPort & ~value; // Which bits transitioned from H to L?

    // If the low bit transitioned from high to low,
    // then increment the row index

    if ((highToLow & 0x01) && keyRow < 9)
      ++keyRow;

    // If the next bit transitioned, then reset the row index

    if (highToLow & 0x02)
      keyRow = 0;

    // Remember for next time

    prevPort = value;
    break;

  default:  // We don't simulation any other ports
    break;
  }
}


//-------------------------------------------------------------------------
//
// Handle the port IN instruction.
//
//-------------------------------------------------------------------------

extern "C" //??
uint8_t portIn(uint8_t port)
{
  switch (port)
  {
  case 0:   // Port 0 is for reading the keyboard columns of the selected row
    return ~keyMatrix[keyRow];

  default:  // We don't simulation any other ports
    return 0;
  }
}


//-------------------------------------------------------------------------
//
// We want unbuffered keyboard input so we don't have to wait for a
// trailing newline.
//
//-------------------------------------------------------------------------

void setUnbufferedInput()
{
    termios settings;
    tcgetattr(0, &settings);

    settings.c_lflag &= (~ICANON);  // Disable line buffered input
    settings.c_lflag &= (~ECHO);    // Disable character echo

    settings.c_cc[VTIME] = 0;   // Timeout
    settings.c_cc[VMIN] = 1;    // Minimum number of characters

    tcsetattr(0, TCSANOW, &settings);
}


//-------------------------------------------------------------------------
//
// Check if there is an input character available. We don't want to block
// the emulation if there is nothing to read.
//
//-------------------------------------------------------------------------

static int numCharsAvailable()
{
    int n;
    ioctl(fileno(stdin), FIONREAD, &n);

    return n;
}


//-------------------------------------------------------------------------
//
// Unfortunately, without raw keyboard input, we only know when a key is
// pressed, not when it is released. So pretend each key is pressed for
// 100ms before being released.
//
// Returns true if we are still waiting for the key to be released.
//
//-------------------------------------------------------------------------

static struct timespec lastTime = {0, 0};

static bool keyDelay()
{
  // No timeout => no key pressed

	if ((lastTime.tv_sec == 0) && (lastTime.tv_nsec == 0))
		return false;

  // Have we waited long enough?

	struct timespec currTime;
	clock_gettime(CLOCK_REALTIME, &currTime);

	if (((currTime.tv_sec - lastTime.tv_sec) * 1000000000
		+ (currTime.tv_nsec - lastTime.tv_nsec)) > 100000000)
	{
    // Timed-out, we've waited long enough

		lastTime = {0, 0};
		return false;
	}

  // Still waiting for the key to be released

	return true;
}


//-------------------------------------------------------------------------
//
// Map keyboard characters to the appropriate row and column for the NASCOM
// keyboard. Taken from the ktab table in
// http://www.nascomhomepage.com/mon/Nassys3.mac
//
// Note, these look inverted compared to the documentation.
// http://www.nascomhomepage.com/pdf/Guide_to_NAS-SYS.pdf hence there is
// a "9-val" later in the code.
//
//-------------------------------------------------------------------------

static uint8_t keyMap[] =
{
  0x00, 0x00, 0x00, 0x00, // #00
  0x00, 0x00, 0x00, 0x00, // #04
  0x00, 0x00, 0x09, 0x00, // #08 \n
  0x00, 0x0e, 0x00, 0x00, // #0c \r
  0x00, 0x00, 0x00, 0x00, // #10
  0x00, 0x00, 0x00, 0x00, // #14
  0x00, 0x00, 0x00, 0x89, // #18 ESC
  0x00, 0x00, 0x00, 0x00, // #1c
	0x14, 0x9c, 0x9b, 0xa3,	// #20  !"#
	0x92, 0xc2, 0xba, 0xb2,	// #24 $%&'
	0xaa, 0xa2, 0x98, 0xa0,	// #28 ()*+
	0x29, 0x0a, 0x21, 0x19,	// #2c ,-./
	0x1a, 0x1c, 0x1b, 0x23,	// #30 0123
	0x12, 0x42, 0x3a, 0x32,	// #34 4567
	0x2a, 0x22, 0x18, 0x20,	// #38 89:;
	0xa9, 0x8a, 0xa1, 0x99,	// #3c <=>?
	0x8d, 0x2c, 0x41, 0x13,	// #40 @ABC
	0x3b, 0x33, 0x43, 0x10,	// #44 DEFG
	0x40, 0x2d, 0x38, 0x30,	// #48 HIJK
	0x28, 0x31, 0x39, 0x25,	// #4c LMNO
	0x1d, 0x24, 0x15, 0x34,	// #50 PQRS
	0x45, 0x35, 0x11, 0x2b,	// #54 TUVW
	0x44, 0x3d, 0x3c, 0x1e,	// #58 XYZ[
	0x9e, 0x16, 0x9a, 0x96,	// #5c \]^_
	0x00, 0xac, 0xc1, 0x93,	// #60 `abc
	0xbb, 0xb3, 0xc3, 0x90,	// #64 defg
	0xc0, 0xad, 0xb8, 0xb0,	// #68 hijk
	0xa8, 0xb1, 0xb9, 0xa5,	// #6c lmno
	0x9d, 0xa4, 0x95, 0xb4,	// #70 pqrs
	0xc5, 0xb5, 0x91, 0xab,	// #74 tuvw
	0xc4, 0xbd, 0xbc, 0x1e,	// #78 xyz{
	0x9e, 0x16, 0x00, 0x08,	// #7c |}~DEL
};


//-------------------------------------------------------------------------
//
// Find the key map corresponding to the entered character.
// Note - what are the CS and CH keys on the NASCOM keyboard?
//
//-------------------------------------------------------------------------

static uint8_t getKey(int numChars)
{
  uint8_t key = 0;
  int ch = getchar();

  // Special handling for escape key, because there may be follow-on
  // characters for the cursor arrow keys.

  if ((ch == '\033') && (numChars == 3))
  {
    getchar();  // Skip

    switch (getchar())
    {
      case 65: key = 0x46; break;  // Up arrow
      case 66: key = 0x36; break;  // Down arrow
      case 67: key = 0x2e; break;  // Right arrow
      case 68: key = 0x3e; break;  // Left arrow
    }
  }
  else
    key = keyMap[ch &= 0x7f];

  return key;
}


//-------------------------------------------------------------------------
//
// Handle any keyboard input. Converts the ASCII character to the
// appropriate row and column in the keyboard map.
//
//-------------------------------------------------------------------------

static queue<uint8_t> keyQueue;

void pollKeyboard()
{
  instructionDelay();

	int n = numCharsAvailable();
	if (n > 0)
  {
    uint8_t key = getKey(n);

    if (key != 0)
      keyQueue.push(key);
  }

	if (keyDelay())
		return;

  // Erase the previous pressed key, but not the shift state

	for (int i = 1; i < 9; ++i)
	  keyMatrix[i] = 0;

  // If there are no more letters in the queue,
  // then erase the shift state too

  if (keyQueue.empty())
  {
		keyMatrix[0] = 0;
    return;
  }

  // Get the next key. If the shift state is different, then
  // toggle the shift key and leave the pressed key for the
  // next time through.

  uint8_t &key = keyQueue.front();

  int row    = 9 - ((key & 0x78) >> 3); // Invert the row
  int col    = key & 0x07;
  bool shift = key & 0x80;

  if (bool(keyMatrix[0] & (1 << 4)) != shift)
    keyMatrix[0] ^= (1 << 4);  // Toggle shift key
  else
  {
	  keyMatrix[row] |= (1 << col);
    keyQueue.pop();
  }

  // Remember when the key was pressed

	clock_gettime(CLOCK_REALTIME, &lastTime);
}
