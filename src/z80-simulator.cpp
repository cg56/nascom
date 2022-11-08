//-------------------------------------------------------------------------
//
// Z80 microprocessor instruction emulator.
//
// This is based on yaze (yet another Z80 emulator) by Frank D. Cringle.
//
//-------------------------------------------------------------------------

#include <stdint.h>

// The caller needs to supply these functions

extern "C" uint8_t readRam(uint16_t addr);
extern "C" void    writeRam(uint16_t addr, uint8_t val);
extern "C" uint8_t portIn(uint8_t port);
extern "C" void    portOut(uint8_t port, uint8_t val);

// All the registers of the Z80

static uint16_t AF;		// Accumulator and flags ?? Separate
static uint16_t BC;		// Rename lower case ?? aReg, flags, bcReg
static uint16_t DE;
static uint16_t HL;
static uint16_t ir;
static uint16_t ix;
static uint16_t iy;
static uint16_t SP;
static uint16_t PC;
static uint16_t IFF;

static uint16_t AFalt;	// Alternate registers
static uint16_t BCalt;
static uint16_t DEalt;
static uint16_t HLalt;

inline uint8_t lowDigit(uint8_t val)	{ return val & 0x0f; }
inline uint8_t highDigit(uint8_t val)	{ return (val >> 4) & 0x0f; }
inline uint8_t lowReg(uint16_t val)		{ return val & 0x00ff; }
inline uint8_t highReg(uint16_t val)	{ return (val >> 8) & 0x00ff; }

#define SetlowReg(x, v)	x = (((x)&0xff00) | ((v)&0xff))
#define SethighReg(x, v) x = (((x)&0xff) | (((v)&0xff) << 8))

/*??
union {
	struct
	{
		uint8_t low;
		uint8_t high;
	}
	uint16_t AF;
} reg;
*/

//-------------------------------------------------------------------------
// Flag handling
//-------------------------------------------------------------------------

const uint8_t CarryFlag		= 0x01;
const uint8_t SubFlag		= 0x02;
const uint8_t ParityFlag	= 0x04;
const uint8_t HalfFlag		= 0x10;
const uint8_t ZeroFlag		= 0x40;
const uint8_t SignFlag		= 0x80;

inline void setFlag(uint8_t flag, bool val)
{
	if (val)
		AF |= flag;
	else
		AF & ~flag;
}

inline bool testFlag(uint8_t flag)
{
	return AF & flag;
}


//-------------------------------------------------------------------------
// Useful functions
//-------------------------------------------------------------------------

inline uint16_t readWord(uint16_t addr)
{
	return readRam(addr) | (readRam(addr+1) << 8);
}


inline void writeWord(uint16_t addr, uint16_t val)
{
	writeRam(addr, val &0xff);
	writeRam(addr+1, val >> 8);
}


static void push(uint16_t val)
{
	writeRam(--SP, val >> 8);
	writeRam(--SP, val & 0xff);
}


static uint16_t pop()
{
	uint16_t val = readRam(SP++);
	val |= (readRam(SP++) << 8);

	return val;
}


static void conditionalJump(bool cond)
{
	if (cond)
		PC = readWord(PC);
	else
		PC += 2;
}


static void conditionalCall(bool cond)
{
    if (cond)
	{
		uint16_t adrr = readWord(PC);
		push(PC+2);
		PC = adrr;
    }
    else
		PC += 2;
}


inline uint8_t parity(uint8_t val)
{
	bool p = true;

	while (val)
	{
		p = !p;
		val = val & (val - 1);
	}

	return p ? 0x04 : 0x00;
}


template<typename T>
inline void swap(T a, T b)
{
	T tmp = a;
	a = b;
	b = tmp;
}


//-------------------------------------------------------------------------
//
// Emulate
//
//-------------------------------------------------------------------------

static void
cb_prefix(uint16_t adr)
{
    unsigned int temp = 0, acu = 0, op, cbits;

		switch ((op = readRam(PC)) & 7) {
		case 0: ++PC; acu = highReg(BC); break;
		case 1: ++PC; acu = lowReg(BC); break;
		case 2: ++PC; acu = highReg(DE); break;
		case 3: ++PC; acu = lowReg(DE); break;
		case 4: ++PC; acu = highReg(HL); break;
		case 5: ++PC; acu = lowReg(HL); break;
		case 6: ++PC; acu = readRam(adr);  break;
		case 7: ++PC; acu = highReg(AF); break;
		}
		switch (op & 0xc0) {
		case 0x00:		/* shift/rotate */
			switch (op & 0x38) {
			case 0x00:	/* RLC */
				temp = (acu << 1) | (acu >> 7);
				cbits = temp & 1;
				goto cbshflg1;
			case 0x08:	/* RRC */
				temp = (acu >> 1) | (acu << 7);
				cbits = temp & 0x80;
				goto cbshflg1;
			case 0x10:	/* RL */
				temp = (acu << 1) | testFlag(CarryFlag);
				cbits = acu & 0x80;
				goto cbshflg1;
			case 0x18:	/* RR */
				temp = (acu >> 1) | (testFlag(CarryFlag) << 7);
				cbits = acu & 1;
				goto cbshflg1;
			case 0x20:	/* SLA */
				temp = acu << 1;
				cbits = acu & 0x80;
				goto cbshflg1;
			case 0x28:	/* SRA */
				temp = (acu >> 1) | (acu & 0x80);
				cbits = acu & 1;
				goto cbshflg1;
			case 0x30:	/* SLIA */
				temp = (acu << 1) | 1;
				cbits = acu & 0x80;
				goto cbshflg1;
			case 0x38:	/* SRL */
				temp = acu >> 1;
				cbits = acu & 1;
			cbshflg1:
				AF = (AF & ~0xff) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					parity(temp) | !!cbits;
			}
			break;
		case 0x40:		/* BIT */
			if (acu & (1 << ((op >> 3) & 7)))
				AF = (AF & ~0xfe) | 0x10 |
				(((op & 0x38) == 0x38) << 7);
			else
				AF = (AF & ~0xfe) | 0x54;
			if ((op&7) != 6)
				AF |= (acu & 0x28);
			temp = acu;
			break;
		case 0x80:		/* RES */
			temp = acu & ~(1 << ((op >> 3) & 7));
			break;
		case 0xc0:		/* SET */
			temp = acu | (1 << ((op >> 3) & 7));
			break;
		}
		switch (op & 7) {
		case 0: SethighReg(BC, temp); break;
		case 1: SetlowReg(BC, temp); break;
		case 2: SethighReg(DE, temp); break;
		case 3: SetlowReg(DE, temp); break;
		case 4: SethighReg(HL, temp); break;
		case 5: SetlowReg(HL, temp); break;
		case 6: writeRam(adr, temp);  break;
		case 7: SethighReg(AF, temp); break;
		}
}

static uint16_t
dfd_prefix(uint16_t IXY)
{
    unsigned int temp, adr, acu, op, sum, cbits;

		switch (++PC, op = readRam(PC-1)) {
		case 0x09:			/* ADD IXY,BC */
			IXY &= 0xffff;
			BC &= 0xffff;
			sum = IXY + BC;
			cbits = (IXY ^ BC ^ sum) >> 8;
			IXY = sum;
			AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x19:			/* ADD IXY,DE */
			IXY &= 0xffff;
			DE &= 0xffff;
			sum = IXY + DE;
			cbits = (IXY ^ DE ^ sum) >> 8;
			IXY = sum;
			AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x21:			/* LD IXY,nnnn */
			IXY = readWord(PC);
			PC += 2;
			break;
		case 0x22:			/* LD (nnnn),IXY */
			temp = readWord(PC);
			writeWord(temp, IXY);
			PC += 2;
			break;
		case 0x23:			/* INC IXY */
			++IXY;
			break;
		case 0x24:			/* INC IXYH */
			IXY += 0x100;
			temp = highReg(IXY);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				(((temp & 0xf) == 0) << 4) |
				((temp == 0x80) << 2);
			break;
		case 0x25:			/* DEC IXYH */
			IXY -= 0x100;
			temp = highReg(IXY);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				(((temp & 0xf) == 0xf) << 4) |
				((temp == 0x7f) << 2) | 2;
			break;
		case 0x26:			/* LD IXYH,nn */
			SethighReg(IXY, readRam(PC)); ++PC;
			break;
		case 0x29:			/* ADD IXY,IXY */
			IXY &= 0xffff;
			sum = IXY + IXY;
			cbits = (IXY ^ IXY ^ sum) >> 8;
			IXY = sum;
			AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x2A:			/* LD IXY,(nnnn) */
			temp = readWord(PC);
			IXY = readWord(temp);
			PC += 2;
			break;
		case 0x2B:			/* DEC IXY */
			--IXY;
			break;
		case 0x2C:			/* INC IXYL */
			temp = lowReg(IXY)+1;
			SetlowReg(IXY, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				(((temp & 0xf) == 0) << 4) |
				((temp == 0x80) << 2);
			break;
		case 0x2D:			/* DEC IXYL */
			temp = lowReg(IXY)-1;
			SetlowReg(IXY, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				(((temp & 0xf) == 0xf) << 4) |
				((temp == 0x7f) << 2) | 2;
			break;
		case 0x2E:			/* LD IXYL,nn */
			SetlowReg(IXY, readRam(PC)); ++PC;
			break;
		case 0x34:			/* INC (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr)+1;
			writeRam(adr, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				(((temp & 0xf) == 0) << 4) |
				((temp == 0x80) << 2);
			break;
		case 0x35:			/* DEC (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr)-1;
			writeRam(adr, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				(((temp & 0xf) == 0xf) << 4) |
				((temp == 0x7f) << 2) | 2;
			break;
		case 0x36:			/* LD (IXY+dd),nn */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, readRam(PC)); ++PC;
			break;
		case 0x39:			/* ADD IXY,SP */
			IXY &= 0xffff;
			SP &= 0xffff;
			sum = IXY + SP;
			cbits = (IXY ^ SP ^ sum) >> 8;
			IXY = sum;
			AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x44:			/* LD B,IXYH */
			SethighReg(BC, highReg(IXY));
			break;
		case 0x45:			/* LD B,IXYL */
			SethighReg(BC, lowReg(IXY));
			break;
		case 0x46:			/* LD B,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SethighReg(BC, readRam(adr));
			break;
		case 0x4C:			/* LD C,IXYH */
			SetlowReg(BC, highReg(IXY));
			break;
		case 0x4D:			/* LD C,IXYL */
			SetlowReg(BC, lowReg(IXY));
			break;
		case 0x4E:			/* LD C,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SetlowReg(BC, readRam(adr));
			break;
		case 0x54:			/* LD D,IXYH */
			SethighReg(DE, highReg(IXY));
			break;
		case 0x55:			/* LD D,IXYL */
			SethighReg(DE, lowReg(IXY));
			break;
		case 0x56:			/* LD D,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SethighReg(DE, readRam(adr));
			break;
		case 0x5C:			/* LD E,H */
			SetlowReg(DE, highReg(IXY));
			break;
		case 0x5D:			/* LD E,L */
			SetlowReg(DE, lowReg(IXY));
			break;
		case 0x5E:			/* LD E,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SetlowReg(DE, readRam(adr));
			break;
		case 0x60:			/* LD IXYH,B */
			SethighReg(IXY, highReg(BC));
			break;
		case 0x61:			/* LD IXYH,C */
			SethighReg(IXY, lowReg(BC));
			break;
		case 0x62:			/* LD IXYH,D */
			SethighReg(IXY, highReg(DE));
			break;
		case 0x63:			/* LD IXYH,E */
			SethighReg(IXY, lowReg(DE));
			break;
		case 0x64:			/* LD IXYH,IXYH */
			/* nop */
			break;
		case 0x65:			/* LD IXYH,IXYL */
			SethighReg(IXY, lowReg(IXY));
			break;
		case 0x66:			/* LD H,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SethighReg(HL, readRam(adr));
			break;
		case 0x67:			/* LD IXYH,A */
			SethighReg(IXY, highReg(AF));
			break;
		case 0x68:			/* LD IXYL,B */
			SetlowReg(IXY, highReg(BC));
			break;
		case 0x69:			/* LD IXYL,C */
			SetlowReg(IXY, lowReg(BC));
			break;
		case 0x6A:			/* LD IXYL,D */
			SetlowReg(IXY, highReg(DE));
			break;
		case 0x6B:			/* LD IXYL,E */
			SetlowReg(IXY, lowReg(DE));
			break;
		case 0x6C:			/* LD IXYL,IXYH */
			SetlowReg(IXY, highReg(IXY));
			break;
		case 0x6D:			/* LD IXYL,IXYL */
			/* nop */
			break;
		case 0x6E:			/* LD L,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SetlowReg(HL, readRam(adr));
			break;
		case 0x6F:			/* LD IXYL,A */
			SetlowReg(IXY, highReg(AF));
			break;
		case 0x70:			/* LD (IXY+dd),B */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, highReg(BC));
			break;
		case 0x71:			/* LD (IXY+dd),C */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, lowReg(BC));
			break;
		case 0x72:			/* LD (IXY+dd),D */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, highReg(DE));
			break;
		case 0x73:			/* LD (IXY+dd),E */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, lowReg(DE));
			break;
		case 0x74:			/* LD (IXY+dd),H */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, highReg(HL));
			break;
		case 0x75:			/* LD (IXY+dd),L */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, lowReg(HL));
			break;
		case 0x77:			/* LD (IXY+dd),A */
			adr = IXY + (signed char) readRam(PC); ++PC;
			writeRam(adr, highReg(AF));
			break;
		case 0x7C:			/* LD A,IXYH */
			SethighReg(AF, highReg(IXY));
			break;
		case 0x7D:			/* LD A,IXYL */
			SethighReg(AF, lowReg(IXY));
			break;
		case 0x7E:			/* LD A,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			SethighReg(AF, readRam(adr));
			break;
		case 0x84:			/* ADD A,IXYH */
			temp = highReg(IXY);
			acu = highReg(AF);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				((cbits >> 8) & 1);
			break;
		case 0x85:			/* ADD A,IXYL */
			temp = lowReg(IXY);
			acu = highReg(AF);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				((cbits >> 8) & 1);
			break;
		case 0x86:			/* ADD A,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr);
			acu = highReg(AF);
			sum = acu + temp;
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				((cbits >> 8) & 1);
			break;
		case 0x8C:			/* ADC A,IXYH */
			temp = highReg(IXY);
			acu = highReg(AF);
			sum = acu + temp + testFlag(CarryFlag);
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				((cbits >> 8) & 1);
			break;
		case 0x8D:			/* ADC A,IXYL */
			temp = lowReg(IXY);
			acu = highReg(AF);
			sum = acu + temp + testFlag(CarryFlag);
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				((cbits >> 8) & 1);
			break;
		case 0x8E:			/* ADC A,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr);
			acu = highReg(AF);
			sum = acu + temp + testFlag(CarryFlag);
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				((cbits >> 8) & 1);
			break;
		case 0x94:			/* SUB IXYH */
			temp = highReg(IXY);
			acu = highReg(AF);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				((cbits >> 8) & 1);
			break;
		case 0x95:			/* SUB IXYL */
			temp = lowReg(IXY);
			acu = highReg(AF);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				((cbits >> 8) & 1);
			break;
		case 0x96:			/* SUB (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr);
			acu = highReg(AF);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				((cbits >> 8) & 1);
			break;
		case 0x9C:			/* SBC A,IXYH */
			temp = highReg(IXY);
			acu = highReg(AF);
			sum = acu - temp - testFlag(CarryFlag);
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				((cbits >> 8) & 1);
			break;
		case 0x9D:			/* SBC A,IXYL */
			temp = lowReg(IXY);
			acu = highReg(AF);
			sum = acu - temp - testFlag(CarryFlag);
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				((cbits >> 8) & 1);
			break;
		case 0x9E:			/* SBC A,(IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr);
			acu = highReg(AF);
			sum = acu - temp - testFlag(CarryFlag);
			cbits = acu ^ temp ^ sum;
			AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
				(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				((cbits >> 8) & 1);
			break;
		case 0xA4:			/* AND IXYH */
			sum = ((AF & (IXY)) >> 8) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) |
				((sum == 0) << 6) | 0x10 | parity(sum);
			break;
		case 0xA5:			/* AND IXYL */
			sum = ((AF >> 8) & IXY) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | 0x10 |
				((sum == 0) << 6) | parity(sum);
			break;
		case 0xA6:			/* AND (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			sum = ((AF >> 8) & readRam(adr)) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | 0x10 |
				((sum == 0) << 6) | parity(sum);
			break;
		case 0xAC:			/* XOR IXYH */
			sum = ((AF ^ (IXY)) >> 8) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
			break;
		case 0xAD:			/* XOR IXYL */
			sum = ((AF >> 8) ^ IXY) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
			break;
		case 0xAE:			/* XOR (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			sum = ((AF >> 8) ^ readRam(adr)) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
			break;
		case 0xB4:			/* OR IXYH */
			sum = ((AF | (IXY)) >> 8) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
			break;
		case 0xB5:			/* OR IXYL */
			sum = ((AF >> 8) | IXY) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
			break;
		case 0xB6:			/* OR (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			sum = ((AF >> 8) | readRam(adr)) & 0xff;
			AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
			break;
		case 0xBC:			/* CP IXYH */
			temp = highReg(IXY);
			AF = (AF & ~0x28) | (temp & 0x28);
			acu = highReg(AF);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xff) | (sum & 0x80) |
				(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0xBD:			/* CP IXYL */
			temp = lowReg(IXY);
			AF = (AF & ~0x28) | (temp & 0x28);
			acu = highReg(AF);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xff) | (sum & 0x80) |
				(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0xBE:			/* CP (IXY+dd) */
			adr = IXY + (signed char) readRam(PC); ++PC;
			temp = readRam(adr);
			AF = (AF & ~0x28) | (temp & 0x28);
			acu = highReg(AF);
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xff) | (sum & 0x80) |
				(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0xCB:			/* CB prefix */
			adr = IXY + (signed char) readRam(PC); ++PC;
			cb_prefix(adr);
			break;
		case 0xE1:			/* pop IXY */
			IXY = pop();
			break;
		case 0xE3:			/* EX (SP),IXY */
			temp = IXY; IXY = pop(); push(temp);
			break;
		case 0xE5:			/* push IXY */
			push(IXY);
			break;
		case 0xE9:			/* JP (IXY) */
			PC = IXY;
			break;
		case 0xF9:			/* LD SP,IXY */
			SP = IXY;
			break;
		default: PC--;		/* ignore DD */
		}
    return(IXY);
}


void z80step()
{
    unsigned int temp, acu, sum, cbits;
    unsigned int op;

    switch(++PC,readRam(PC-1)) {
	case 0x00:			/* NOP */
		break;
	case 0x01:			/* LD BC,nnnn */
		BC = readWord(PC);
		PC += 2;
		break;
	case 0x02:			/* LD (BC),A */
		writeRam(BC, highReg(AF));
		break;
	case 0x03:			/* INC BC */
		++BC;
		break;
	case 0x04:			/* INC B */
		BC += 0x100;
		temp = highReg(BC);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x05:			/* DEC B */
		BC -= 0x100;
		temp = highReg(BC);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x06:			/* LD B,nn */
		SethighReg(BC, readRam(PC)); ++PC;
		break;
	case 0x07:			/* RLCA */
		AF = ((AF >> 7) & 0x0128) | ((AF << 1) & ~0x1ff) |
			(AF & 0xc4) | ((AF >> 15) & 1);
		break;
	case 0x08:			/* EX AF,AF' */
		swap(AF, AFalt);
		break;
	case 0x09:			/* ADD HL,BC */
		HL &= 0xffff;
		BC &= 0xffff;
		sum = HL + BC;
		cbits = (HL ^ BC ^ sum) >> 8;
		HL = sum;
		AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0x0A:			/* LD A,(BC) */
		SethighReg(AF, readRam(BC));
		break;
	case 0x0B:			/* DEC BC */
		--BC;
		break;
	case 0x0C:			/* INC C */
		temp = lowReg(BC)+1;
		SetlowReg(BC, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x0D:			/* DEC C */
		temp = lowReg(BC)-1;
		SetlowReg(BC, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x0E:			/* LD C,nn */
		SetlowReg(BC, readRam(PC)); ++PC;
		break;
	case 0x0F:			/* RRCA */
		temp = highReg(AF);
		sum = temp >> 1;
		AF = ((temp & 1) << 15) | (sum << 8) |
			(sum & 0x28) | (AF & 0xc4) | (temp & 1);
		break;
	case 0x10:			/* DJNZ dd */
		PC += ((BC -= 0x100) & 0xff00) ? (signed char) readRam(PC) + 1 : 1;
		break;
	case 0x11:			/* LD DE,nnnn */
		DE = readWord(PC);
		PC += 2;
		break;
	case 0x12:			/* LD (DE),A */
		writeRam(DE, highReg(AF));
		break;
	case 0x13:			/* INC DE */
		++DE;
		break;
	case 0x14:			/* INC D */
		DE += 0x100;
		temp = highReg(DE);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x15:			/* DEC D */
		DE -= 0x100;
		temp = highReg(DE);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x16:			/* LD D,nn */
		SethighReg(DE, readRam(PC)); ++PC;
		break;
	case 0x17:			/* RLA */
		AF = ((AF << 8) & 0x0100) | ((AF >> 7) & 0x28) | ((AF << 1) & ~0x01ff) |
			(AF & 0xc4) | ((AF >> 15) & 1);
		break;
	case 0x18:			/* JR dd */
		PC += (1) ? (signed char) readRam(PC) + 1 : 1;
		break;
	case 0x19:			/* ADD HL,DE */
		HL &= 0xffff;
		DE &= 0xffff;
		sum = HL + DE;
		cbits = (HL ^ DE ^ sum) >> 8;
		HL = sum;
		AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0x1A:			/* LD A,(DE) */
		SethighReg(AF, readRam(DE));
		break;
	case 0x1B:			/* DEC DE */
		--DE;
		break;
	case 0x1C:			/* INC E */
		temp = lowReg(DE)+1;
		SetlowReg(DE, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x1D:			/* DEC E */
		temp = lowReg(DE)-1;
		SetlowReg(DE, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x1E:			/* LD E,nn */
		SetlowReg(DE, readRam(PC)); ++PC;
		break;
	case 0x1F:			/* RRA */
		temp = highReg(AF);
		sum = temp >> 1;
		AF = ((AF & 1) << 15) | (sum << 8) |
			(sum & 0x28) | (AF & 0xc4) | (temp & 1);
		break;
	case 0x20:			/* JR NZ,dd */
		PC += (!testFlag(ZeroFlag)) ? (signed char) readRam(PC) + 1 : 1;
		break;
	case 0x21:			/* LD HL,nnnn */
		HL = readWord(PC);
		PC += 2;
		break;
	case 0x22:			/* LD (nnnn),HL */
		temp = readWord(PC);
		writeWord(temp, HL);
		PC += 2;
		break;
	case 0x23:			/* INC HL */
		++HL;
		break;
	case 0x24:			/* INC H */
		HL += 0x100;
		temp = highReg(HL);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x25:			/* DEC H */
		HL -= 0x100;
		temp = highReg(HL);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x26:			/* LD H,nn */
		SethighReg(HL, readRam(PC)); ++PC;
		break;
	case 0x27:			/* DAA */
		acu = highReg(AF);
		temp = lowDigit(acu);
		cbits = testFlag(CarryFlag);
		if (testFlag(SubFlag)) {	/* last operation was a subtract */
			int hd = cbits || acu > 0x99;
			if (testFlag(HalfFlag) || (temp > 9)) { /* adjust low digit */
				if (temp > 5)
					setFlag(HalfFlag, 0);
				acu -= 6;
				acu &= 0xff;
			}
			if (hd)		/* adjust high digit */
				acu -= 0x160;
		}
		else {			/* last operation was an add */
			if (testFlag(HalfFlag) || (temp > 9)) { /* adjust low digit */
				setFlag(HalfFlag, (temp > 9));
				acu += 6;
			}
			if (cbits || ((acu & 0x1f0) > 0x90)) /* adjust high digit */
				acu += 0x60;
		}
		cbits |= (acu >> 8) & 1;
		acu &= 0xff;
		AF = (acu << 8) | (acu & 0xa8) | ((acu == 0) << 6) |
			(AF & 0x12) | parity(acu) | cbits;
		break;
	case 0x28:			/* JR Z,dd */
		PC += (testFlag(ZeroFlag)) ? (signed char) readRam(PC) + 1 : 1;
		break;
	case 0x29:			/* ADD HL,HL */
		HL &= 0xffff;
		sum = HL + HL;
		cbits = (HL ^ HL ^ sum) >> 8;
		HL = sum;
		AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0x2A:			/* LD HL,(nnnn) */
		temp = readWord(PC);
		HL = readWord(temp);
		PC += 2;
		break;
	case 0x2B:			/* DEC HL */
		--HL;
		break;
	case 0x2C:			/* INC L */
		temp = lowReg(HL)+1;
		SetlowReg(HL, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x2D:			/* DEC L */
		temp = lowReg(HL)-1;
		SetlowReg(HL, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x2E:			/* LD L,nn */
		SetlowReg(HL, readRam(PC)); ++PC;
		break;
	case 0x2F:			/* CPL */
		AF = (~AF & ~0xff) | (AF & 0xc5) | ((~AF >> 8) & 0x28) | 0x12;
		break;
	case 0x30:			/* JR NC,dd */
		PC += (!testFlag(CarryFlag)) ? (signed char) readRam(PC) + 1 : 1;
		break;
	case 0x31:			/* LD SP,nnnn */
		SP = readWord(PC);
		PC += 2;
		break;
	case 0x32:			/* LD (nnnn),A */
		temp = readWord(PC);
		writeRam(temp, highReg(AF));
		PC += 2;
		break;
	case 0x33:			/* INC SP */
		++SP;
		break;
	case 0x34:			/* INC (HL) */
		temp = readRam(HL)+1;
		writeRam(HL, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x35:			/* DEC (HL) */
		temp = readRam(HL)-1;
		writeRam(HL, temp);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x36:			/* LD (HL),nn */
		writeRam(HL, readRam(PC)); ++PC;
		break;
	case 0x37:			/* SCF */
		AF = (AF&~0x3b)|((AF>>8)&0x28)|1;
		break;
	case 0x38:			/* JR C,dd */
		PC += (testFlag(CarryFlag)) ? (signed char) readRam(PC) + 1 : 1;
		break;
	case 0x39:			/* ADD HL,SP */
		HL &= 0xffff;
		SP &= 0xffff;
		sum = HL + SP;
		cbits = (HL ^ SP ^ sum) >> 8;
		HL = sum;
		AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0x3A:			/* LD A,(nnnn) */
		temp = readWord(PC);
		SethighReg(AF, readRam(temp));
		PC += 2;
		break;
	case 0x3B:			/* DEC SP */
		--SP;
		break;
	case 0x3C:			/* INC A */
		AF += 0x100;
		temp = highReg(AF);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0) << 4) |
			((temp == 0x80) << 2);
		break;
	case 0x3D:			/* DEC A */
		AF -= 0x100;
		temp = highReg(AF);
		AF = (AF & ~0xfe) | (temp & 0xa8) |
			(((temp & 0xff) == 0) << 6) |
			(((temp & 0xf) == 0xf) << 4) |
			((temp == 0x7f) << 2) | 2;
		break;
	case 0x3E:			/* LD A,nn */
		SethighReg(AF, readRam(PC)); ++PC;
		break;
	case 0x3F:			/* CCF */
		AF = (AF&~0x3b)|((AF>>8)&0x28)|((AF&1)<<4)|(~AF&1);
		break;
	case 0x40:			/* LD B,B */
		/* nop */
		break;
	case 0x41:			/* LD B,C */
		BC = (BC & 255) | ((BC & 255) << 8);
		break;
	case 0x42:			/* LD B,D */
		BC = (BC & 255) | (DE & ~255);
		break;
	case 0x43:			/* LD B,E */
		BC = (BC & 255) | ((DE & 255) << 8);
		break;
	case 0x44:			/* LD B,H */
		BC = (BC & 255) | (HL & ~255);
		break;
	case 0x45:			/* LD B,L */
		BC = (BC & 255) | ((HL & 255) << 8);
		break;
	case 0x46:			/* LD B,(HL) */
		SethighReg(BC, readRam(HL));
		break;
	case 0x47:			/* LD B,A */
		BC = (BC & 255) | (AF & ~255);
		break;
	case 0x48:			/* LD C,B */
		BC = (BC & ~255) | ((BC >> 8) & 255);
		break;
	case 0x49:			/* LD C,C */
		/* nop */
		break;
	case 0x4A:			/* LD C,D */
		BC = (BC & ~255) | ((DE >> 8) & 255);
		break;
	case 0x4B:			/* LD C,E */
		BC = (BC & ~255) | (DE & 255);
		break;
	case 0x4C:			/* LD C,H */
		BC = (BC & ~255) | ((HL >> 8) & 255);
		break;
	case 0x4D:			/* LD C,L */
		BC = (BC & ~255) | (HL & 255);
		break;
	case 0x4E:			/* LD C,(HL) */
		SetlowReg(BC, readRam(HL));
		break;
	case 0x4F:			/* LD C,A */
		BC = (BC & ~255) | ((AF >> 8) & 255);
		break;
	case 0x50:			/* LD D,B */
		DE = (DE & 255) | (BC & ~255);
		break;
	case 0x51:			/* LD D,C */
		DE = (DE & 255) | ((BC & 255) << 8);
		break;
	case 0x52:			/* LD D,D */
		/* nop */
		break;
	case 0x53:			/* LD D,E */
		DE = (DE & 255) | ((DE & 255) << 8);
		break;
	case 0x54:			/* LD D,H */
		DE = (DE & 255) | (HL & ~255);
		break;
	case 0x55:			/* LD D,L */
		DE = (DE & 255) | ((HL & 255) << 8);
		break;
	case 0x56:			/* LD D,(HL) */
		SethighReg(DE, readRam(HL));
		break;
	case 0x57:			/* LD D,A */
		DE = (DE & 255) | (AF & ~255);
		break;
	case 0x58:			/* LD E,B */
		DE = (DE & ~255) | ((BC >> 8) & 255);
		break;
	case 0x59:			/* LD E,C */
		DE = (DE & ~255) | (BC & 255);
		break;
	case 0x5A:			/* LD E,D */
		DE = (DE & ~255) | ((DE >> 8) & 255);
		break;
	case 0x5B:			/* LD E,E */
		/* nop */
		break;
	case 0x5C:			/* LD E,H */
		DE = (DE & ~255) | ((HL >> 8) & 255);
		break;
	case 0x5D:			/* LD E,L */
		DE = (DE & ~255) | (HL & 255);
		break;
	case 0x5E:			/* LD E,(HL) */
		SetlowReg(DE, readRam(HL));
		break;
	case 0x5F:			/* LD E,A */
		DE = (DE & ~255) | ((AF >> 8) & 255);
		break;
	case 0x60:			/* LD H,B */
		HL = (HL & 255) | (BC & ~255);
		break;
	case 0x61:			/* LD H,C */
		HL = (HL & 255) | ((BC & 255) << 8);
		break;
	case 0x62:			/* LD H,D */
		HL = (HL & 255) | (DE & ~255);
		break;
	case 0x63:			/* LD H,E */
		HL = (HL & 255) | ((DE & 255) << 8);
		break;
	case 0x64:			/* LD H,H */
		/* nop */
		break;
	case 0x65:			/* LD H,L */
		HL = (HL & 255) | ((HL & 255) << 8);
		break;
	case 0x66:			/* LD H,(HL) */
		SethighReg(HL, readRam(HL));
		break;
	case 0x67:			/* LD H,A */
		HL = (HL & 255) | (AF & ~255);
		break;
	case 0x68:			/* LD L,B */
		HL = (HL & ~255) | ((BC >> 8) & 255);
		break;
	case 0x69:			/* LD L,C */
		HL = (HL & ~255) | (BC & 255);
		break;
	case 0x6A:			/* LD L,D */
		HL = (HL & ~255) | ((DE >> 8) & 255);
		break;
	case 0x6B:			/* LD L,E */
		HL = (HL & ~255) | (DE & 255);
		break;
	case 0x6C:			/* LD L,H */
		HL = (HL & ~255) | ((HL >> 8) & 255);
		break;
	case 0x6D:			/* LD L,L */
		/* nop */
		break;
	case 0x6E:			/* LD L,(HL) */
		SetlowReg(HL, readRam(HL));
		break;
	case 0x6F:			/* LD L,A */
		HL = (HL & ~255) | ((AF >> 8) & 255);
		break;
	case 0x70:			/* LD (HL),B */
		writeRam(HL, highReg(BC));
		break;
	case 0x71:			/* LD (HL),C */
		writeRam(HL, lowReg(BC));
		break;
	case 0x72:			/* LD (HL),D */
		writeRam(HL, highReg(DE));
		break;
	case 0x73:			/* LD (HL),E */
		writeRam(HL, lowReg(DE));
		break;
	case 0x74:			/* LD (HL),H */
		writeRam(HL, highReg(HL));
		break;
	case 0x75:			/* LD (HL),L */
		writeRam(HL, lowReg(HL));
		break;
	case 0x76:			/* HALT */
		return;
	case 0x77:			/* LD (HL),A */
		writeRam(HL, highReg(AF));
		break;
	case 0x78:			/* LD A,B */
		AF = (AF & 255) | (BC & ~255);
		break;
	case 0x79:			/* LD A,C */
		AF = (AF & 255) | ((BC & 255) << 8);
		break;
	case 0x7A:			/* LD A,D */
		AF = (AF & 255) | (DE & ~255);
		break;
	case 0x7B:			/* LD A,E */
		AF = (AF & 255) | ((DE & 255) << 8);
		break;
	case 0x7C:			/* LD A,H */
		AF = (AF & 255) | (HL & ~255);
		break;
	case 0x7D:			/* LD A,L */
		AF = (AF & 255) | ((HL & 255) << 8);
		break;
	case 0x7E:			/* LD A,(HL) */
		SethighReg(AF, readRam(HL));
		break;
	case 0x7F:			/* LD A,A */
		/* nop */
		break;
	case 0x80:			/* ADD A,B */
		temp = highReg(BC);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x81:			/* ADD A,C */
		temp = lowReg(BC);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x82:			/* ADD A,D */
		temp = highReg(DE);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x83:			/* ADD A,E */
		temp = lowReg(DE);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x84:			/* ADD A,H */
		temp = highReg(HL);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x85:			/* ADD A,L */
		temp = lowReg(HL);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x86:			/* ADD A,(HL) */
		temp = readRam(HL);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x87:			/* ADD A,A */
		temp = highReg(AF);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x88:			/* ADC A,B */
		temp = highReg(BC);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x89:			/* ADC A,C */
		temp = lowReg(BC);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x8A:			/* ADC A,D */
		temp = highReg(DE);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x8B:			/* ADC A,E */
		temp = lowReg(DE);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x8C:			/* ADC A,H */
		temp = highReg(HL);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x8D:			/* ADC A,L */
		temp = lowReg(HL);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x8E:			/* ADC A,(HL) */
		temp = readRam(HL);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x8F:			/* ADC A,A */
		temp = highReg(AF);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		break;
	case 0x90:			/* SUB B */
		temp = highReg(BC);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x91:			/* SUB C */
		temp = lowReg(BC);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x92:			/* SUB D */
		temp = highReg(DE);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x93:			/* SUB E */
		temp = lowReg(DE);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x94:			/* SUB H */
		temp = highReg(HL);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x95:			/* SUB L */
		temp = lowReg(HL);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x96:			/* SUB (HL) */
		temp = readRam(HL);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x97:			/* SUB A */
		temp = highReg(AF);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x98:			/* SBC A,B */
		temp = highReg(BC);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x99:			/* SBC A,C */
		temp = lowReg(BC);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x9A:			/* SBC A,D */
		temp = highReg(DE);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x9B:			/* SBC A,E */
		temp = lowReg(DE);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x9C:			/* SBC A,H */
		temp = highReg(HL);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x9D:			/* SBC A,L */
		temp = lowReg(HL);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x9E:			/* SBC A,(HL) */
		temp = readRam(HL);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0x9F:			/* SBC A,A */
		temp = highReg(AF);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		break;
	case 0xA0:			/* AND B */
		sum = ((AF & (BC)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) |
			((sum == 0) << 6) | 0x10 | parity(sum);
		break;
	case 0xA1:			/* AND C */
		sum = ((AF >> 8) & BC) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | 0x10 |
			((sum == 0) << 6) | parity(sum);
		break;
	case 0xA2:			/* AND D */
		sum = ((AF & (DE)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) |
			((sum == 0) << 6) | 0x10 | parity(sum);
		break;
	case 0xA3:			/* AND E */
		sum = ((AF >> 8) & DE) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | 0x10 |
			((sum == 0) << 6) | parity(sum);
		break;
	case 0xA4:			/* AND H */
		sum = ((AF & (HL)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) |
			((sum == 0) << 6) | 0x10 | parity(sum);
		break;
	case 0xA5:			/* AND L */
		sum = ((AF >> 8) & HL) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | 0x10 |
			((sum == 0) << 6) | parity(sum);
		break;
	case 0xA6:			/* AND (HL) */
		sum = ((AF >> 8) & readRam(HL)) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | 0x10 |
			((sum == 0) << 6) | parity(sum);
		break;
	case 0xA7:			/* AND A */
		sum = ((AF & (AF)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) |
			((sum == 0) << 6) | 0x10 | parity(sum);
		break;
	case 0xA8:			/* XOR B */
		sum = ((AF ^ (BC)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xA9:			/* XOR C */
		sum = ((AF >> 8) ^ BC) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xAA:			/* XOR D */
		sum = ((AF ^ (DE)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xAB:			/* XOR E */
		sum = ((AF >> 8) ^ DE) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xAC:			/* XOR H */
		sum = ((AF ^ (HL)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xAD:			/* XOR L */
		sum = ((AF >> 8) ^ HL) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xAE:			/* XOR (HL) */
		sum = ((AF >> 8) ^ readRam(HL)) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xAF:			/* XOR A */
		sum = ((AF ^ (AF)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB0:			/* OR B */
		sum = ((AF | (BC)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB1:			/* OR C */
		sum = ((AF >> 8) | BC) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB2:			/* OR D */
		sum = ((AF | (DE)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB3:			/* OR E */
		sum = ((AF >> 8) | DE) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB4:			/* OR H */
		sum = ((AF | (HL)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB5:			/* OR L */
		sum = ((AF >> 8) | HL) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB6:			/* OR (HL) */
		sum = ((AF >> 8) | readRam(HL)) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB7:			/* OR A */
		sum = ((AF | (AF)) >> 8) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		break;
	case 0xB8:			/* CP B */
		temp = highReg(BC);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xB9:			/* CP C */
		temp = lowReg(BC);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xBA:			/* CP D */
		temp = highReg(DE);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xBB:			/* CP E */
		temp = lowReg(DE);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xBC:			/* CP H */
		temp = highReg(HL);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xBD:			/* CP L */
		temp = lowReg(HL);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xBE:			/* CP (HL) */
		temp = readRam(HL);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xBF:			/* CP A */
		temp = highReg(AF);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		break;
	case 0xC0:			/* RET NZ */
		if (!testFlag(ZeroFlag)) PC = pop();
		break;
	case 0xC1:			/* pop BC */
		BC = pop();
		break;
	case 0xC2:			/* JP NZ,nnnn */
		conditionalJump(!testFlag(ZeroFlag));
		break;
	case 0xC3:			/* JP nnnn */
		conditionalJump(true);
		break;
	case 0xC4:			/* CALL NZ,nnnn */
		conditionalCall(!testFlag(ZeroFlag));
		break;
	case 0xC5:			/* push BC */
		push(BC);
		break;
	case 0xC6:			/* ADD A,nn */
		temp = readRam(PC);
		acu = highReg(AF);
		sum = acu + temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		++PC;
		break;
	case 0xC7:			/* RST 0 */
		push(PC); PC = 0;
		break;
	case 0xC8:			/* RET Z */
		if (testFlag(ZeroFlag)) PC= pop();
		break;
	case 0xC9:			/* RET */
		PC = pop();
		break;
	case 0xCA:			/* JP Z,nnnn */
		conditionalJump(testFlag(ZeroFlag));
		break;
	case 0xCB:			/* CB prefix */
		cb_prefix(HL);
		break;
	case 0xCC:			/* CALL Z,nnnn */
		conditionalCall(testFlag(ZeroFlag));
		break;
	case 0xCD:			/* CALL nnnn */
		conditionalCall(true);
		break;
	case 0xCE:			/* ADC A,nn */
		temp = readRam(PC);
		acu = highReg(AF);
		sum = acu + temp + testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) |
			((cbits >> 8) & 1);
		++PC;
		break;
	case 0xCF:			/* RST 8 */
		push(PC); PC = 8;
		break;
	case 0xD0:			/* RET NC */
		if (!testFlag(CarryFlag)) PC = pop();
		break;
	case 0xD1:			/* pop DE */
		DE = pop();
		break;
	case 0xD2:			/* JP NC,nnnn */
		conditionalJump(!testFlag(CarryFlag));
		break;
	case 0xD3:			/* OUT (nn),A */
		portOut(readRam(PC), highReg(AF)); ++PC;
		break;
	case 0xD4:			/* CALL NC,nnnn */
		conditionalCall(!testFlag(CarryFlag));
		break;
	case 0xD5:			/* push DE */
		push(DE);
		break;
	case 0xD6:			/* SUB nn */
		temp = readRam(PC);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		++PC;
		break;
	case 0xD7:			/* RST 10H */
		push(PC); PC = 0x10;
		break;
	case 0xD8:			/* RET C */
		if (testFlag(CarryFlag)) PC = pop();
		break;
	case 0xD9:			/* EXX */
		swap(BC, BCalt);
		swap(DE, DEalt);
		swap(HL, HLalt);
		break;
	case 0xDA:			/* JP C,nnnn */
		conditionalJump(testFlag(CarryFlag));
		break;
	case 0xDB:			/* IN A,(nn) */
		SethighReg(AF, portIn(readRam(PC))); ++PC;
		break;
	case 0xDC:			/* CALL C,nnnn */
		conditionalCall(testFlag(CarryFlag));
		break;
	case 0xDD:			/* DD prefix */
		ix = dfd_prefix(ix);
		break;
	case 0xDE:			/* SBC A,nn */
		temp = readRam(PC);
		acu = highReg(AF);
		sum = acu - temp - testFlag(CarryFlag);
		cbits = acu ^ temp ^ sum;
		AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
			(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			((cbits >> 8) & 1);
		++PC;
		break;
	case 0xDF:			/* RST 18H */
		push(PC); PC = 0x18;
		break;
	case 0xE0:			/* RET PO */
		if (!testFlag(ParityFlag)) PC = pop();
		break;
	case 0xE1:			/* pop HL */
		HL = pop();
		break;
	case 0xE2:			/* JP PO,nnnn */
		conditionalJump(!testFlag(ParityFlag));
		break;
	case 0xE3:			/* EX (SP),HL */
		temp = HL; HL = pop(); push(temp);
		break;
	case 0xE4:			/* CALL PO,nnnn */
		conditionalCall(!testFlag(ParityFlag));
		break;
	case 0xE5:			/* push HL */
		push(HL);
		break;
	case 0xE6:			/* AND nn */
		sum = ((AF >> 8) & readRam(PC)) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | 0x10 |
			((sum == 0) << 6) | parity(sum);
		++PC;
		break;
	case 0xE7:			/* RST 20H */
		push(PC); PC = 0x20;
		break;
	case 0xE8:			/* RET PE */
		if (testFlag(ParityFlag)) PC = pop();
		break;
	case 0xE9:			/* JP (HL) */
		PC = HL;
		break;
	case 0xEA:			/* JP PE,nnnn */
		conditionalJump(testFlag(ParityFlag));
		break;
	case 0xEB:			/* EX DE,HL */
		temp = HL; HL = DE; DE = temp;
		break;
	case 0xEC:			/* CALL PE,nnnn */
		conditionalCall(testFlag(ParityFlag));
		break;
	case 0xED:			/* ED prefix */
		switch (++PC, op = readRam(PC-1)) {
		case 0x40:			/* IN B,(C) */
			temp = portIn(lowReg(BC));
			SethighReg(BC, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x41:			/* OUT (C),B */
			portOut(lowReg(BC), BC);
			break;
		case 0x42:			/* SBC HL,BC */
			HL &= 0xffff;
			BC &= 0xffff;
			sum = HL - BC - testFlag(CarryFlag);
			cbits = (HL ^ BC ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
			break;
		case 0x43:			/* LD (nnnn),BC */
			temp = readWord(PC);
			writeWord(temp, BC);
			PC += 2;
			break;
		case 0x44:			/* NEG */
			temp = highReg(AF);
			AF = (-(AF & 0xff00) & 0xff00);
			AF |= ((AF >> 8) & 0xa8) | (((AF & 0xff00) == 0) << 6) |
				(((temp & 0x0f) != 0) << 4) | ((temp == 0x80) << 2) |
				2 | (temp != 0);
			break;
		case 0x45:			/* RETN */
			IFF |= IFF >> 1;
			PC = pop();
			break;
		case 0x46:			/* IM 0 */
			/* interrupt mode 0 */
			break;
		case 0x47:			/* LD I,A */
			ir = (ir & 255) | (AF & ~255);
			break;
		case 0x48:			/* IN C,(C) */
			temp = portIn(lowReg(BC));
			SetlowReg(BC, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x49:			/* OUT (C),C */
			portOut(lowReg(BC), BC);
			break;
		case 0x4A:			/* ADC HL,BC */
			HL &= 0xffff;
			BC &= 0xffff;
			sum = HL + BC + testFlag(CarryFlag);
			cbits = (HL ^ BC ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x4B:			/* LD BC,(nnnn) */
			temp = readWord(PC);
			BC = readWord(temp);
			PC += 2;
			break;
		case 0x4D:			/* RETI */
			IFF |= IFF >> 1;
			PC = pop();
			break;
		case 0x4F:			/* LD R,A */
			ir = (ir & ~255) | ((AF >> 8) & 255);
			break;
		case 0x50:			/* IN D,(C) */
			temp = portIn(lowReg(BC));
			SethighReg(DE, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x51:			/* OUT (C),D */
			portOut(lowReg(BC), DE);
			break;
		case 0x52:			/* SBC HL,DE */
			HL &= 0xffff;
			DE &= 0xffff;
			sum = HL - DE - testFlag(CarryFlag);
			cbits = (HL ^ DE ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
			break;
		case 0x53:			/* LD (nnnn),DE */
			temp = readWord(PC);
			writeWord(temp, DE);
			PC += 2;
			break;
		case 0x56:			/* IM 1 */
			/* interrupt mode 1 */
			break;
		case 0x57:			/* LD A,I */
			AF = (AF & 0x29) | (ir & ~255) | ((ir >> 8) & 0x80) | (((ir & ~255) == 0) << 6) | ((IFF & 2) << 1);
			break;
		case 0x58:			/* IN E,(C) */
			temp = portIn(lowReg(BC));
			SetlowReg(DE, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x59:			/* OUT (C),E */
			portOut(lowReg(BC), DE);
			break;
		case 0x5A:			/* ADC HL,DE */
			HL &= 0xffff;
			DE &= 0xffff;
			sum = HL + DE + testFlag(CarryFlag);
			cbits = (HL ^ DE ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x5B:			/* LD DE,(nnnn) */
			temp = readWord(PC);
			DE = readWord(temp);
			PC += 2;
			break;
		case 0x5E:			/* IM 2 */
			/* interrupt mode 2 */
			break;
		case 0x5F:			/* LD A,R */
			AF = (AF & 0x29) | ((ir & 255) << 8) | (ir & 0x80) | (((ir & 255) == 0) << 6) | ((IFF & 2) << 1);
			break;
		case 0x60:			/* IN H,(C) */
			temp = portIn(lowReg(BC));
			SethighReg(HL, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x61:			/* OUT (C),H */
			portOut(lowReg(BC), HL);
			break;
		case 0x62:			/* SBC HL,HL */
			HL &= 0xffff;
			sum = HL - HL - testFlag(CarryFlag);
			cbits = (HL ^ HL ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
			break;
		case 0x63:			/* LD (nnnn),HL */
			temp = readWord(PC);
			writeWord(temp, HL);
			PC += 2;
			break;
		case 0x67:			/* RRD */
			temp = readRam(HL);
			acu = highReg(AF);
			writeRam(HL, highDigit(temp) | (lowDigit(acu) << 4));
			acu = (acu & 0xf0) | lowDigit(temp);
			AF = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) |
				parity(acu) | (AF & 1);
			break;
		case 0x68:			/* IN L,(C) */
			temp = portIn(lowReg(BC));
			SetlowReg(HL, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x69:			/* OUT (C),L */
			portOut(lowReg(BC), HL);
			break;
		case 0x6A:			/* ADC HL,HL */
			HL &= 0xffff;
			sum = HL + HL + testFlag(CarryFlag);
			cbits = (HL ^ HL ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x6B:			/* LD HL,(nnnn) */
			temp = readWord(PC);
			HL = readWord(temp);
			PC += 2;
			break;
		case 0x6F:			/* RLD */
			temp = readRam(HL);
			acu = highReg(AF);
			writeRam(HL, (lowDigit(temp) << 4) | lowDigit(acu));
			acu = (acu & 0xf0) | highDigit(temp);
			AF = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) |
				parity(acu) | (AF & 1);
			break;
		case 0x70:			/* IN (C) */
			temp = portIn(lowReg(BC));
			SetlowReg(temp, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x71:			/* OUT (C),0 */
			portOut(lowReg(BC), 0);
			break;
		case 0x72:			/* SBC HL,SP */
			HL &= 0xffff;
			SP &= 0xffff;
			sum = HL - SP - testFlag(CarryFlag);
			cbits = (HL ^ SP ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
			break;
		case 0x73:			/* LD (nnnn),SP */
			temp = readWord(PC);
			writeWord(temp, SP);
			PC += 2;
			break;
		case 0x78:			/* IN A,(C) */
			temp = portIn(lowReg(BC));
			SethighReg(AF, temp);
			AF = (AF & ~0xfe) | (temp & 0xa8) |
				(((temp & 0xff) == 0) << 6) |
				parity(temp);
			break;
		case 0x79:			/* OUT (C),A */
			portOut(lowReg(BC), AF);
			break;
		case 0x7A:			/* ADC HL,SP */
			HL &= 0xffff;
			SP &= 0xffff;
			sum = HL + SP + testFlag(CarryFlag);
			cbits = (HL ^ SP ^ sum) >> 8;
			HL = sum;
			AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
				(((sum & 0xffff) == 0) << 6) |
				(((cbits >> 6) ^ (cbits >> 5)) & 4) |
				(cbits & 0x10) | ((cbits >> 8) & 1);
			break;
		case 0x7B:			/* LD SP,(nnnn) */
			temp = readWord(PC);
			SP = readWord(temp);
			PC += 2;
			break;
		case 0xA0:			/* LDI */
			acu = readRam(HL); ++HL;
			writeRam(DE, acu); ++DE;
			acu += highReg(AF);
			AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
				(((--BC & 0xffff) != 0) << 2);
			break;
		case 0xA1:			/* CPI */
			acu = highReg(AF);
			temp = readRam(HL); ++HL;
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
				(((sum - ((cbits&16)>>4))&2) << 4) | (cbits & 16) |
				((sum - ((cbits >> 4) & 1)) & 8) |
				((--BC & 0xffff) != 0) << 2 | 2;
			if ((sum & 15) == 8 && (cbits & 16) != 0)
				AF &= ~8;
			break;
		case 0xA2:			/* INI */
			writeRam(HL, portIn(lowReg(BC))); ++HL;
			setFlag(SubFlag, 1);
			setFlag(ParityFlag, (--BC & 0xffff) != 0);
			break;
		case 0xA3:			/* OUTI */
			portOut(lowReg(BC), readRam(HL)); ++HL;
			setFlag(SubFlag, 1);
			SethighReg(BC, lowReg(BC) - 1);
			setFlag(ZeroFlag, lowReg(BC) == 0);
			break;
		case 0xA8:			/* LDD */
			acu = readRam(HL); --HL;
			writeRam(DE, acu); --DE;
			acu += highReg(AF);
			AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
				(((--BC & 0xffff) != 0) << 2);
			break;
		case 0xA9:			/* CPD */
			acu = highReg(AF);
			temp = readRam(HL); --HL;
			sum = acu - temp;
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
				(((sum - ((cbits&16)>>4))&2) << 4) | (cbits & 16) |
				((sum - ((cbits >> 4) & 1)) & 8) |
				((--BC & 0xffff) != 0) << 2 | 2;
			if ((sum & 15) == 8 && (cbits & 16) != 0)
				AF &= ~8;
			break;
		case 0xAA:			/* IND */
			writeRam(HL, portIn(lowReg(BC))); --HL;
			setFlag(SubFlag, 1);
			SethighReg(BC, lowReg(BC) - 1);
			setFlag(ZeroFlag, lowReg(BC) == 0);
			break;
		case 0xAB:			/* OUTD */
			portOut(lowReg(BC), readRam(HL)); --HL;
			setFlag(SubFlag, 1);
			SethighReg(BC, lowReg(BC) - 1);
			setFlag(ZeroFlag, lowReg(BC) == 0);
			break;
		case 0xB0:			/* LDIR */
			acu = highReg(AF);
			BC &= 0xffff;
			do {
				acu = readRam(HL); ++HL;
				writeRam(DE, acu); ++DE;
			} while (--BC);
			acu += highReg(AF);
			AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
			break;
		case 0xB1:			/* CPIR */
			acu = highReg(AF);
			BC &= 0xffff;
			do {
				temp = readRam(HL); ++HL;
				op = --BC != 0;
				sum = acu - temp;
			} while (op && sum != 0);
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
				(((sum - ((cbits&16)>>4))&2) << 4) |
				(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
				op << 2 | 2;
			if ((sum & 15) == 8 && (cbits & 16) != 0)
				AF &= ~8;
			break;
		case 0xB2:			/* INIR */
			temp = highReg(BC);
			do {
				writeRam(HL, portIn(lowReg(BC))); ++HL;
			} while (--temp);
			SethighReg(BC, 0);
			setFlag(SubFlag, 1);
			setFlag(ZeroFlag, 1);
			break;
		case 0xB3:			/* OTIR */
			temp = highReg(BC);
			do {
				portOut(lowReg(BC), readRam(HL)); ++HL;
			} while (--temp);
			SethighReg(BC, 0);
			setFlag(SubFlag, 1);
			setFlag(ZeroFlag, 1);
			break;
		case 0xB8:			/* LDDR */
			BC &= 0xffff;
			do {
				acu = readRam(HL); --HL;
				writeRam(DE, acu); --DE;
			} while (--BC);
			acu += highReg(AF);
			AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
			break;
		case 0xB9:			/* CPDR */
			acu = highReg(AF);
			BC &= 0xffff;
			do {
				temp = readRam(HL); --HL;
				op = --BC != 0;
				sum = acu - temp;
			} while (op && sum != 0);
			cbits = acu ^ temp ^ sum;
			AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
				(((sum - ((cbits&16)>>4))&2) << 4) |
				(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
				op << 2 | 2;
			if ((sum & 15) == 8 && (cbits & 16) != 0)
				AF &= ~8;
			break;
		case 0xBA:			/* INDR */
			temp = highReg(BC);
			do {
				writeRam(HL, portIn(lowReg(BC))); --HL;
			} while (--temp);
			SethighReg(BC, 0);
			setFlag(SubFlag, 1);
			setFlag(ZeroFlag, 1);
			break;
		case 0xBB:			/* OTDR */
			temp = highReg(BC);
			do {
				portOut(lowReg(BC), readRam(HL)); --HL;
			} while (--temp);
			SethighReg(BC, 0);
			setFlag(SubFlag, 1);
			setFlag(ZeroFlag, 1);
			break;
		default: if (0x40 <= op && op <= 0x7f) PC--;		/* ignore ED */
		}
		break;
	case 0xEE:			/* XOR nn */
		sum = ((AF >> 8) ^ readRam(PC)) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		++PC;
		break;
	case 0xEF:			/* RST 28H */
		push(PC); PC = 0x28;
		break;
	case 0xF0:			/* RET P */
		if (!testFlag(SignFlag)) PC = pop();
		break;
	case 0xF1:			/* pop AF */
		AF = pop();
		break;
	case 0xF2:			/* JP P,nnnn */
		conditionalJump(!testFlag(SignFlag));
		break;
	case 0xF3:			/* DI */
		IFF = 0;
		break;
	case 0xF4:			/* CALL P,nnnn */
		conditionalCall(!testFlag(SignFlag));
		break;
	case 0xF5:			/* push AF */
		push(AF);
		break;
	case 0xF6:			/* OR nn */
		sum = ((AF >> 8) | readRam(PC)) & 0xff;
		AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | parity(sum);
		++PC;
		break;
	case 0xF7:			/* RST 30H */
		push(PC); PC = 0x30;
		break;
	case 0xF8:			/* RET M */
		if (testFlag(SignFlag)) PC = pop();
		break;
	case 0xF9:			/* LD SP,HL */
		SP = HL;
		break;
	case 0xFA:			/* JP M,nnnn */
		conditionalJump(testFlag(SignFlag));
		break;
	case 0xFB:			/* EI */
		IFF = 3;
		break;
	case 0xFC:			/* CALL M,nnnn */
		conditionalCall(testFlag(SignFlag));
		break;
	case 0xFD:			/* FD prefix */
		iy = dfd_prefix(iy);
		break;
	case 0xFE:			/* CP nn */
		temp = readRam(PC);
		AF = (AF & ~0x28) | (temp & 0x28);
		acu = highReg(AF);
		sum = acu - temp;
		cbits = acu ^ temp ^ sum;
		AF = (AF & ~0xff) | (sum & 0x80) |
			(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
			(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
			(cbits & 0x10) | ((cbits >> 8) & 1);
		++PC;
		break;
	case 0xFF:			/* RST 38H */
		push(PC); PC = 0x38;
    }
}
