#include <iostream>
#include <fstream>

using namespace std;


// The Z80 can address 64K of memory.

/*static*/ uint8_t ram[64*1024] = {'\0'};


//-------------------------------------------------------------------------
//
// Make sure the character is printable on the screen. Unfortunately we
// don't have the full 256-character NASCOM character set.
//
//-------------------------------------------------------------------------

inline uint8_t printable(uint8_t ch) { return max(ch & 0x7f, 0x20); }


//-------------------------------------------------------------------------
//
// Update the entire screen in one go. This isn't the most optimal, we
// could just update the character that changed. However we're quick enough
// these days that nobody will notice any flash (especially because we
// don't erase the screen beforehand).
//
// The screens isn't very big, the dimensions are 48 character X 16 lines.
//
//-------------------------------------------------------------------------

static void updateScreen()
{
  uint8_t * const screen = &ram[0x80a];	// Start of video memory

	cout << "[H";		// Cursor home to top left

  // According to the documentation, line 15 is at the top!
  // It's used for a status display that doesn't scroll up.
  // http://www.nascomhomepage.com/pdf/Guide_to_NAS-SYS.pdf

  for (int x = 0; x < 48; ++x)
    cout << printable(screen[15*64+x]);
  cout << "\n";

  // Now do the rest of the lines

	for (int y = 0; y < 15; ++y)
	{
		for (int x = 0; x < 48; ++x)
			cout << printable(screen[y*64+x]);

		cout << "\n";
	}

  // All done, flush to make sure everything appears

	cout.flush();
}


//-------------------------------------------------------------------------
//
// All 64K of ram is readable, so nothing exciting here.
//
//-------------------------------------------------------------------------

extern "C"  //??
uint8_t readRam(uint16_t addr)
{
  return ram[addr];
}


//-------------------------------------------------------------------------
//
// Special handling for the various writable regions of memory.
//
//-------------------------------------------------------------------------

extern "C"  //??
void writeRam(uint16_t addr, uint8_t val)
{
  // Don't overwrite read-only ROM locations

  if ((addr >= 0x800) && (addr < 0xe000))
	  ram[addr] = val;

  // Did we write to the screen?

	if ((addr >= 0x800) && (addr <= 0x800+1024))
		updateScreen();
}


//-------------------------------------------------------------------------
//
// Load a .nas format file into the memory.
//
//-------------------------------------------------------------------------

void loadNasFile(const string &filename)
{
  ifstream f(filename, std::fstream::in);
  if (!f.is_open())
  {
    cerr << "Cannot open " << filename << endl;
    exit(1);
  }

  string line;
  while (getline(f, line))
  {
    if (line[0] == '.')
      return;

    uint16_t addr;
    uint8_t  v[8];

    if (sscanf(line.c_str(), "%hx %hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx",
      &addr, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 9)
    {
      ram[addr+0] = v[0];
      ram[addr+1] = v[1];
      ram[addr+2] = v[2];
      ram[addr+3] = v[3];
      ram[addr+4] = v[4];
      ram[addr+5] = v[5];
      ram[addr+6] = v[6];
      ram[addr+7] = v[7];
    }
    else
    {
      cerr << "Malformed line " << line << endl;
      exit(1);
    }
  }
}
